#include "net_client.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <chrono>

#include "debug_stats.h"
#include "log.h"
#include "protocol.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kHeartbeatInterval = std::chrono::milliseconds(1000);
constexpr auto kLinkTimeout = std::chrono::milliseconds(5000);
constexpr unsigned kReconnectDelayMs = 2000;
constexpr unsigned kSleepSliceMs = 50;
constexpr long kSelectTimeoutUs = 5000;
constexpr int kSendTimeoutMs = 1000;
constexpr size_t kRecvChunk = 8192;
constexpr size_t kMaxQueuedFrames = 4096;
constexpr size_t kHelloFixedSize = 3;

SOCKET connectLoopback(uint16_t port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    const BOOL noDelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
    const int sendTimeout = kSendTimeoutMs;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&sendTimeout), sizeof(sendTimeout));
    return s;
}

bool sendAll(SOCKET s, const std::vector<uint8_t>& bytes) {
    size_t sent = 0;
    while (sent < bytes.size()) {
        const int n = send(s, reinterpret_cast<const char*>(bytes.data()) + sent,
                           static_cast<int>(bytes.size() - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool readable(SOCKET s) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    timeval timeout{0, kSelectTimeoutUs};
    return select(0, &set, nullptr, nullptr, &timeout) > 0;
}

std::vector<uint8_t> helloPayload(const std::string& gameId) {
    std::vector<uint8_t> p(kHelloFixedSize + gameId.size());
    std::memcpy(p.data(), &proto::kVersion, sizeof(proto::kVersion));
    p[2] = static_cast<uint8_t>(gameId.size());
    std::memcpy(p.data() + kHelloFixedSize, gameId.data(), gameId.size());
    return p;
}

}  // namespace

void NetClient::start(uint16_t port, std::string gameId, TickHandler onTick) {
    if (running_.exchange(true)) return;
    port_ = port;
    gameId_ = std::move(gameId);
    onTick_ = std::move(onTick);
    thread_ = std::thread([this] { run(); });
}

void NetClient::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

bool NetClient::send(uint16_t type, bool reliable, uint8_t destSlot, std::span<const uint8_t> payload) {
    auto frame = proto::encodeFrame(type, reliable ? proto::kFlagReliable : 0, destSlot, payload);
    std::lock_guard lock(mutex_);
    if (!session_.linked) return false;
    if (outbox_.size() >= kMaxQueuedFrames) outbox_.erase(outbox_.begin());
    outbox_.push_back(std::move(frame));
    return true;
}

SessionSnapshot NetClient::poll(const FrameHandler& handler) {
    std::vector<GameFrame> frames;
    SessionSnapshot snapshot;
    {
        std::lock_guard lock(mutex_);
        frames.swap(inbox_);
        snapshot = session_;
    }
    if (handler) {
        for (const auto& frame : frames) handler(frame);
    }
    return snapshot;
}

void NetClient::run() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        logger::write("net: WSAStartup failed");
        return;
    }
    while (running_) {
        const SOCKET s = connectLoopback(port_);
        if (s != INVALID_SOCKET) {
            logger::write("net: connected to launcher on port %u", port_);
            serve(s);
            closesocket(s);
            resetSession();
            logger::write("net: link down");
        }
        sleepWhileRunning(kReconnectDelayMs);
    }
    WSACleanup();
}

void NetClient::serve(uintptr_t socket) {
    const SOCKET s = static_cast<SOCKET>(socket);
    if (!sendAll(s, proto::encodeFrame(proto::kHello, proto::kFlagReliable, 0, helloPayload(gameId_)))) return;

    std::vector<uint8_t> rx;
    uint8_t chunk[kRecvChunk];
    auto lastRx = Clock::now();
    auto lastHeartbeat = lastRx;
    const auto heartbeat = proto::encodeFrame(proto::kHeartbeat, 0, 0, {});

    while (running_) {
        if (readable(s)) {
            const int n = recv(s, reinterpret_cast<char*>(chunk), sizeof(chunk), 0);
            if (n <= 0) return;
            rx.insert(rx.end(), chunk, chunk + n);
            lastRx = Clock::now();
            if (!parseFrames(rx)) return;
        }
        if (!flushOutbox(socket)) return;

        const auto now = Clock::now();
        if (now - lastHeartbeat >= kHeartbeatInterval) {
            lastHeartbeat = now;
            if (!sendAll(s, heartbeat)) return;
        }
        if (now - lastRx > kLinkTimeout) {
            logger::write("net: heartbeat timeout");
            debug_stats::setError("net: heartbeat timeout");
            return;
        }
        if (onTick_) onTick_();
    }
}

bool NetClient::parseFrames(std::vector<uint8_t>& rx) {
    size_t offset = 0;
    bool ok = true;
    while (rx.size() - offset >= proto::kLenFieldSize) {
        const uint32_t len = proto::readLe<uint32_t>(rx.data() + offset);
        if (len < proto::kHeaderSize || len > proto::kMaxFrameLen) {
            logger::write("net: invalid frame length %u", len);
            ok = false;
            break;
        }
        if (rx.size() - offset < proto::kLenFieldSize + len) break;
        const uint8_t* body = rx.data() + offset + proto::kLenFieldSize;
        const std::span<const uint8_t> payload(body + proto::kHeaderSize, len - proto::kHeaderSize);
        offset += proto::kLenFieldSize + len;
        if (!handleFrame(proto::readLe<uint16_t>(body), body[2], body[3], payload)) {
            ok = false;
            break;
        }
    }
    rx.erase(rx.begin(), rx.begin() + static_cast<std::ptrdiff_t>(offset));
    return ok;
}

bool NetClient::handleFrame(uint16_t type, uint8_t flags, uint8_t slot, std::span<const uint8_t> payload) {
    std::lock_guard lock(mutex_);
    switch (type) {
        case proto::kWelcome:
            if (payload.size() < proto::kWelcomeSize) return true;
            session_ = {};
            session_.linked = true;
            session_.localSlot = payload[0];
            session_.hostSlot = payload[1];
            session_.maxPlayers = payload[2];
            session_.epoch = proto::readLe<uint32_t>(payload.data() + 3);
            inbox_.clear();
            logger::write("net: WELCOME local=%u host=%u max=%u epoch=%u", session_.localSlot,
                          session_.hostSlot, session_.maxPlayers, session_.epoch);
            return true;
        case proto::kPeerUp: {
            if (payload.size() < proto::kPeerUpFixedSize) return true;
            const size_t nameLen = std::min<size_t>(payload[9], payload.size() - proto::kPeerUpFixedSize);
            PeerInfo peer;
            peer.slot = payload[0];
            peer.steamId = proto::readLe<uint64_t>(payload.data() + 1);
            peer.name.assign(reinterpret_cast<const char*>(payload.data()) + proto::kPeerUpFixedSize, nameLen);
            peer.joinSerial = ++joinCounter_;
            std::erase_if(session_.peers, [&](const PeerInfo& p) { return p.slot == peer.slot; });
            logger::write("net: PEER_UP slot=%u name=%s", peer.slot, peer.name.c_str());
            session_.peers.push_back(std::move(peer));
            return true;
        }
        case proto::kPeerDown:
            if (payload.empty()) return true;
            std::erase_if(session_.peers, [&](const PeerInfo& p) { return p.slot == payload[0]; });
            logger::write("net: PEER_DOWN slot=%u", payload[0]);
            return true;
        case proto::kReject: {
            const size_t len = payload.empty() ? 0 : std::min<size_t>(payload[0], payload.size() - 1);
            const std::string reason = len ? std::string(reinterpret_cast<const char*>(payload.data()) + 1, len) : "";
            logger::write("net: REJECT %s", reason.c_str());
            debug_stats::setError("net: rejected: %s", reason.c_str());
            return false;
        }
        case proto::kPeerStats:
            if (payload.size() < proto::kPeerStatsSize) return true;
            for (auto& peer : session_.peers) {
                if (peer.slot == payload[0]) peer.rttMs = proto::readLe<uint16_t>(payload.data() + 1);
            }
            return true;
        case proto::kHeartbeat:
            return true;
        default:
            break;
    }
    if (type >= proto::kFirstGameType && session_.linked) {
        if (inbox_.size() >= kMaxQueuedFrames) inbox_.erase(inbox_.begin());
        inbox_.push_back({type, flags, slot, std::vector<uint8_t>(payload.begin(), payload.end())});
    }
    return true;
}

bool NetClient::flushOutbox(uintptr_t socket) {
    std::vector<std::vector<uint8_t>> frames;
    {
        std::lock_guard lock(mutex_);
        frames.swap(outbox_);
    }
    for (const auto& frame : frames) {
        if (!sendAll(static_cast<SOCKET>(socket), frame)) return false;
    }
    return true;
}

void NetClient::resetSession() {
    std::lock_guard lock(mutex_);
    session_ = {};
    inbox_.clear();
    outbox_.clear();
}

void NetClient::sleepWhileRunning(unsigned ms) {
    for (unsigned waited = 0; running_ && waited < ms; waited += kSleepSliceMs) Sleep(kSleepSliceMs);
}
