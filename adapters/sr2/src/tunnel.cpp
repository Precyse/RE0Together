#include "tunnel.h"

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>

#include <algorithm>
#include <mutex>
#include <set>
#include <span>
#include <vector>

#include "code_patch.h"
#include "game_frame.h"
#include "lan_address.h"
#include "log.h"
#include "memory.h"
#include "net_client.h"
#include "rate_limit.h"
#include "sr2_addresses.h"
#include "tunnel_rules.h"
#include "tunnel_sites.h"

namespace {

using namespace tunnel_rules;

constexpr uint16_t kLauncherPort = 27962;  // RE0 27960, DS2 27980
constexpr const char* kGameId = "sr2";
constexpr uint64_t kCounterReportMs = 5000;
constexpr DWORD kMsPerSecond = 1000;
constexpr DWORD kMicrosPerMs = 1000;
constexpr int kSockaddrSize = sizeof(sockaddr_in);

struct Datagram {
    uint32_t source;
    std::vector<uint8_t> bytes;
};

// What the tunnel knows of the session, rebuilt from the launcher's snapshot.
struct Session {
    bool linked = false;
    bool isHost = false;
    uint8_t hostSlot = 0;
    std::optional<uint32_t> own;
    std::vector<Mapped> peers;  // the other members that have an address
};

NetClient& g_client = *new NetClient;  // never destroyed: its thread must outlive the static destruction at exit
bool g_trace = false;
uintptr_t g_gameSocketVariable = sr2::kGameSocket;
HANDLE g_arrived = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // auto-reset: a datagram was added to the inbox

std::mutex g_mutex;  // guards everything below
Session g_session;
Inbox<Datagram> g_inbox;
std::set<uint32_t> g_loggedNotVirtual;
std::set<uint64_t> g_loggedCollisions;

Counters g_counters;
CounterValues g_reported;
rate_limit::RateLimit g_reportLimit(kCounterReportMs);

SOCKET gameSocket() { return readDword(g_gameSocketVariable); }

void logPeers(const Session& session) {
    for (const Mapped& peer : session.peers) {
        logger::write("trace: tunnel peer slot %u steam id %llu address %s", peer.slot, peer.steamId,
                      formatAddress(peer.address).c_str());
    }
}

// Rebuilds the member table from a snapshot (under g_mutex). Our own address comes from our SteamID, peers from PEER_UP.
void applySnapshot(const SessionSnapshot& snapshot) {
    Session next;
    next.linked = snapshot.linked;
    if (snapshot.linked) {
        next.isHost = snapshot.localSlot == snapshot.hostSlot;
        next.hostSlot = snapshot.hostSlot;
        std::vector<Member> members;
        for (const PeerInfo& peer : snapshot.peers) members.push_back({peer.steamId, peer.slot});
        if (snapshot.localSteamId != 0) {
            members.push_back({snapshot.localSteamId, snapshot.localSlot});
            next.own = virtualAddress(snapshot.localSteamId);
        }
        const Roster roster = mapMembers(members);
        for (const Mapped& member : roster.members) {
            if (member.steamId != snapshot.localSteamId) next.peers.push_back(member);
        }
        for (const Collision& collision : roster.collisions) {
            if (!g_loggedCollisions.insert(collision.dropped).second) continue;
            logger::write("tunnel: steam id %llu shares virtual address %s with %llu", collision.dropped,
                          formatAddress(collision.address).c_str(), collision.kept);
        }
    }
    if (next.own && next.own != g_session.own) logger::write("tunnel: own address %s", formatAddress(*next.own).c_str());
    const bool peersChanged = next.peers != g_session.peers;
    g_session = std::move(next);
    if (g_trace && peersChanged) logPeers(g_session);
}

void refresh() {
    const SessionSnapshot snapshot = g_client.poll({});
    std::lock_guard lock(g_mutex);
    applySnapshot(snapshot);
}

void reportCounters() {
    const CounterValues now = g_counters.values();
    uint32_t triggers = 0;
    if (now == g_reported || !g_reportLimit.allow(GetTickCount64(), triggers)) return;
    g_reported = now;
    logger::write("tunnel: %s", describe(now).c_str());
    if (g_trace) logger::write("trace: tunnel per peer: %s", describeSlots(g_counters).c_str());
}

void onTick() {
    refresh();
    reportCounters();
}

// A frame from another adapter (on the launcher link's thread).
void onFrame(const GameFrame& frame) {
    if (frame.type != kGameDatagramType) return;
    const auto decoded = decode(frame.payload);
    if (!decoded) {
        ++g_counters.oversize;
        return;
    }
    refresh();
    bool droppedOldest = false;
    {
        std::lock_guard lock(g_mutex);
        const InboundResult result = acceptInbound(decoded->destination, g_session.own.value_or(0), frame.slot, g_session.peers);
        if (result.verdict == Inbound::WrongDestination) {
            ++g_counters.wrongDestination;
            return;
        }
        if (result.verdict == Inbound::NoPeer) {
            ++g_counters.noPeer;
            return;
        }
        droppedOldest = g_inbox.push({result.source, std::vector<uint8_t>(decoded->datagram.begin(), decoded->datagram.end())},
                                     GetTickCount64());
    }
    if (droppedOldest) ++g_counters.inboxFull;
    ++g_counters.received;
    g_counters.countReceivedFrom(frame.slot);
    if (decoded->datagram.size() > kLargeDatagram) ++g_counters.large;
    SetEvent(g_arrived);
}

void forward(const char* buffer, int length, const sockaddr* to, int toLength) {
    if (!to || toLength < kSockaddrSize || to->sa_family != AF_INET) {
        ++g_counters.notVirtual;
        return;
    }
    const uint32_t destination = ntohl(reinterpret_cast<const sockaddr_in*>(to)->sin_addr.s_addr);
    refresh();
    Routing routing;
    bool firstNotVirtual = false;
    {
        std::lock_guard lock(g_mutex);
        routing = classify(destination, g_session.isHost, g_session.hostSlot, g_session.peers);
        firstNotVirtual = routing.route == Route::NotVirtual && g_loggedNotVirtual.insert(destination).second;
    }
    if (routing.route == Route::NoPeer) {
        ++g_counters.noPeer;
        return;
    }
    if (routing.route == Route::NotVirtual) {
        ++g_counters.notVirtual;
        if (firstNotVirtual) logger::write("tunnel: the game sent to %s, which is not a virtual address", formatAddress(destination).c_str());
        return;
    }
    const std::span<const uint8_t> datagram(reinterpret_cast<const uint8_t*>(buffer), length > 0 ? static_cast<size_t>(length) : 0);
    const auto payload = encode(destination, datagram);
    if (!payload) {
        ++g_counters.oversize;
        return;
    }
    if (!g_client.send(kGameDatagramType, false, routing.slot, *payload)) {
        ++g_counters.noPeer;  // the link went down after the table was read
        return;
    }
    ++g_counters.sent;
    if (routing.route == Route::Peer) g_counters.countSentTo(routing.slot);
    if (datagram.size() > kLargeDatagram) ++g_counters.large;
}

// S1: the game's sendto. Always reports the datagram as sent, like UDP.
int WSAAPI sendToTunnel(SOCKET s, const char* buffer, int length, int flags, const sockaddr* to, int toLength) {
    if (s != gameSocket()) return sendto(s, buffer, length, flags, to, toLength);
    forward(buffer, length, to, toLength);
    return length;
}

bool isGameSocketOnly(const fd_set* readSet, const fd_set* writeSet, const fd_set* errorSet) {
    return readSet && !writeSet && !errorSet && readSet->fd_count == 1 && readSet->fd_array[0] == gameSocket();
}

DWORD waitMilliseconds(const timeval* timeout) {
    if (!timeout) return INFINITE;
    return static_cast<DWORD>(timeout->tv_sec) * kMsPerSecond + (static_cast<DWORD>(timeout->tv_usec) + kMicrosPerMs - 1) / kMicrosPerMs;
}

// While the game thread is stopped its net thread reads nothing: the datagrams wait here, where the newest are kept.
bool held() { return holdsInbox(GetCurrentThreadId() == game_frame::threadId(), game_frame::lastFrameMs(), GetTickCount64()); }

bool inboxHasDatagram() {
    if (held()) return false;
    std::lock_guard lock(g_mutex);
    return !g_inbox.empty();
}

// S2, S5: "is the game socket readable" is "is the inbox not empty" (and not held). The read set is left as given when it is,
// so the __WSAFDIsSet that follows says yes.
int WSAAPI selectReadable(int nfds, fd_set* readSet, fd_set* writeSet, fd_set* errorSet, const timeval* timeout) {
    if (!isGameSocketOnly(readSet, writeSet, errorSet)) return select(nfds, readSet, writeSet, errorSet, timeout);
    const DWORD waitMs = waitMilliseconds(timeout);
    const ULONGLONG start = GetTickCount64();
    for (;;) {
        if (inboxHasDatagram()) return 1;
        const ULONGLONG elapsed = GetTickCount64() - start;
        if (waitMs != INFINITE && elapsed >= waitMs) break;
        const DWORD remaining = waitMs == INFINITE ? INFINITE : static_cast<DWORD>(waitMs - elapsed);
        if (WaitForSingleObject(g_arrived, remaining) == WAIT_TIMEOUT) break;
    }
    readSet->fd_count = 0;
    return 0;
}

// S4: the game's recvfrom pops one datagram; the sender is its virtual address at the shipped port.
int WSAAPI receiveFromTunnel(SOCKET s, char* buffer, int length, int flags, sockaddr* from, int* fromLength) {
    if (s != gameSocket()) return recvfrom(s, buffer, length, flags, from, fromLength);
    std::optional<Datagram> datagram;
    uint64_t stale = 0;
    if (!held()) {
        std::lock_guard lock(g_mutex);
        datagram = g_inbox.pop(GetTickCount64(), stale);
    }
    g_counters.stale += stale;
    if (!datagram) {
        WSASetLastError(WSAEWOULDBLOCK);
        return SOCKET_ERROR;
    }
    const int size = std::min<int>(length, static_cast<int>(datagram->bytes.size()));
    memcpy(buffer, datagram->bytes.data(), static_cast<size_t>(size));
    if (from && fromLength && *fromLength >= kSockaddrSize) {
        sockaddr_in source{};
        source.sin_family = AF_INET;
        source.sin_port = htons(static_cast<u_short>(sr2::kGamePort));
        source.sin_addr.s_addr = htonl(datagram->source);
        memcpy(from, &source, sizeof(source));
        *fromLength = kSockaddrSize;
    }
    return size;
}

}  // namespace

namespace tunnel {

Calls calls() {
    return {reinterpret_cast<uintptr_t>(&sendToTunnel), reinterpret_cast<uintptr_t>(&selectReadable),
            reinterpret_cast<uintptr_t>(&receiveFromTunnel)};
}

void startLink(bool trace, uintptr_t gameSocketVariable) {
    g_trace = trace;
    g_gameSocketVariable = gameSocketVariable;
    g_client.start(kLauncherPort, kGameId, onTick, onFrame);
}

bool install(Transport transport, bool trace) {
    if (transport == Transport::Native) {
        logger::write("tunnel: native transport, game sockets untouched");
        return false;
    }
    lan_address::useVirtualRule(&ownAddress);
    const Calls replacements = calls();
    const tunnel_sites::Thunks thunks{replacements.sendTo, replacements.selectReadable, replacements.receiveFrom,
                                      lan_address::thunk()};
    if (!applyCodePatches(tunnel_sites::patches(transport, thunks))) {
        lan_address::useVirtualRule(nullptr);
        logger::write("tunnel: sites differ, steam transport off");
        return false;
    }
    startLink(trace, sr2::kGameSocket);
    logger::write("tunnel: steam transport, launcher port %u", kLauncherPort);
    return true;
}

std::optional<uint32_t> ownAddress() {
    std::lock_guard lock(g_mutex);
    return g_session.own;
}

std::optional<uint32_t> hostAddress() {
    std::lock_guard lock(g_mutex);
    if (!g_session.linked) return std::nullopt;
    if (g_session.isHost) return g_session.own;
    const auto host = std::find_if(g_session.peers.begin(), g_session.peers.end(),
                                   [](const Mapped& peer) { return peer.slot == g_session.hostSlot; });
    return host == g_session.peers.end() ? std::nullopt : std::optional<uint32_t>(host->address);
}

}  // namespace tunnel
