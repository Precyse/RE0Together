#include "cargo_transfer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>

#include "log.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kRefreshInterval = std::chrono::milliseconds(500);
constexpr auto kAddRetryInterval = std::chrono::milliseconds(300);
constexpr auto kAddExpiry = std::chrono::seconds(10);  // a piece that cannot be created by then is dropped
constexpr auto kReportInterval = std::chrono::seconds(5);  // the list is re-sent this often even unchanged

enum class Action { Give, Take };

struct Request {
    Action action;
    game::Cargo piece;
};

std::mutex g_mutex;  // guards the three below (net thread writes, render thread reads and queues)
std::vector<game::Cargo> g_local;
std::optional<cargo_transfer::Partner> g_partner;
std::vector<Request> g_requests;

std::atomic<bool> g_host{false};
std::atomic<uint8_t> g_hostSlot{0};
std::vector<uint32_t> g_awaited;  // host, net thread: kinds asked of the guest whose CARGO_ADD has not come yet
struct PendingAdd {
    game::Cargo piece;
    Clock::time_point since;
};
std::vector<PendingAdd> g_pendingAdds;  // net thread: pieces received whose creation must wait
std::vector<cargo_transfer::CargoEntry> g_reported;  // net thread: the list sent last
Clock::time_point g_lastRefresh;
Clock::time_point g_lastReport;

cargo_transfer::CargoEntry toEntry(const game::Cargo& piece) {
    cargo_transfer::CargoEntry entry{piece.handle, piece.type, {}, piece.orderId, piece.secondId, piece.durability, piece.category, {}};
    std::memcpy(entry.name, piece.name.data(), std::min(piece.name.size(), sizeof(entry.name)));
    return entry;
}

game::Cargo fromEntry(const cargo_transfer::CargoEntry& entry) {
    return {entry.handle, entry.type, std::string(entry.name, strnlen(entry.name, sizeof(entry.name))), entry.orderId,
            entry.secondId, entry.category, entry.durability};
}

bool sameEntries(const std::vector<cargo_transfer::CargoEntry>& a, const std::vector<cargo_transfer::CargoEntry>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const auto& x, const auto& y) {
               return x.handle == y.handle && x.type == y.type && x.orderId == y.orderId;
           });
}

std::optional<cargo_transfer::Partner> parseList(uint8_t slot, const std::vector<uint8_t>& payload) {
    uint32_t count = 0;
    if (payload.size() < sizeof(count)) return std::nullopt;
    std::memcpy(&count, payload.data(), sizeof(count));
    if (count > cargo_transfer::kMaxListed || payload.size() != sizeof(count) + count * sizeof(cargo_transfer::CargoEntry)) {
        return std::nullopt;
    }
    cargo_transfer::Partner partner{slot, {}};
    for (uint32_t i = 0; i < count; ++i) {
        cargo_transfer::CargoEntry entry;
        std::memcpy(&entry, payload.data() + sizeof(count) + i * sizeof(entry), sizeof(entry));
        partner.cargo.push_back(fromEntry(entry));
    }
    return partner;
}

game::Cargo pieceOf(const cargo_transfer::CargoAdd& add) {
    return {0, add.type, {}, add.orderId, add.secondId, add.category, add.durability};
}

// Guest: the host asked for one of our pieces. It goes only if we still carry it.
void giveUp(NetClient& net, uint8_t hostSlot, uint64_t handle) {
    const std::vector<game::Cargo> carried = game::carriedCargo();
    const auto piece = std::find_if(carried.begin(), carried.end(), [&](const auto& c) { return c.handle == handle; });
    if (piece == carried.end() || !game::removeCargo(handle)) {
        logger::write("cargo: host asked for %llx, not carried", static_cast<unsigned long long>(handle));
        return;
    }
    cargo_transfer::sendPiece(net, hostSlot, *piece);
    logger::write("cargo: gave %s (%u) to the host", piece->name.c_str(), piece->type);
}

// Receives a piece: created at once, or kept and retried while the game removes the stale copy of it.
void receive(const game::Cargo& piece) {
    switch (game::addCargo(piece)) {
        case game::AddResult::Done:
            break;
        case game::AddResult::Retry:
            g_pendingAdds.push_back({piece, Clock::now()});
            break;
        case game::AddResult::Refused:
            logger::write("cargo: could not receive %s (%u)", piece.name.c_str(), piece.type);
            break;
    }
}

// Host: one piece of the given kind arrived from the guest; only kinds we asked for are accepted.
void received(const game::Cargo& piece) {
    const auto awaited = std::find(g_awaited.begin(), g_awaited.end(), piece.type);
    if (awaited == g_awaited.end()) {
        logger::write("cargo: unrequested piece %u from the guest ignored", piece.type);
        return;
    }
    g_awaited.erase(awaited);
    receive(piece);
    logger::write("cargo: took %u from the guest (order %llx)", piece.type, static_cast<unsigned long long>(piece.orderId));
}

void retryPendingAdds(const Clock::time_point now) {
    static Clock::time_point last;
    if (g_pendingAdds.empty() || now - last < kAddRetryInterval) return;
    last = now;
    std::vector<PendingAdd> waiting;
    for (const PendingAdd& pending : g_pendingAdds) {
        if (now - pending.since > kAddExpiry) {
            logger::write("cargo: gave up receiving %s (%u)", pending.piece.name.c_str(), pending.piece.type);
        } else if (game::addCargo(pending.piece) == game::AddResult::Retry) {
            waiting.push_back(pending);
        }
    }
    g_pendingAdds = std::move(waiting);
}

void refreshLocal(const Clock::time_point now) {
    if (now - g_lastRefresh < kRefreshInterval) return;
    g_lastRefresh = now;
    std::vector<game::Cargo> carried = game::carriedCargo();
    std::lock_guard lock(g_mutex);
    g_local = std::move(carried);
}

void report(NetClient& net, const Clock::time_point now) {
    std::vector<cargo_transfer::CargoEntry> entries;
    {
        std::lock_guard lock(g_mutex);
        for (const game::Cargo& piece : g_local) {
            if (entries.size() == cargo_transfer::kMaxListed) break;
            entries.push_back(toEntry(piece));
        }
    }
    if (sameEntries(entries, g_reported) && now - g_lastReport < kReportInterval) return;
    const auto count = static_cast<uint32_t>(entries.size());
    std::vector<uint8_t> payload(sizeof(count) + count * sizeof(cargo_transfer::CargoEntry));
    std::memcpy(payload.data(), &count, sizeof(count));
    if (count) std::memcpy(payload.data() + sizeof(count), entries.data(), count * sizeof(cargo_transfer::CargoEntry));
    if (!net.send(cargo_transfer::kMsgCargoList, true, proto::kSlotAll, payload)) return;
    g_reported = std::move(entries);
    g_lastReport = now;
}

void forgetDepartedPartner(const SessionSnapshot& session) {
    std::lock_guard lock(g_mutex);
    const bool here = g_partner && std::any_of(session.peers.begin(), session.peers.end(),
                                               [](const PeerInfo& peer) { return peer.slot == g_partner->slot; });
    if (!here) g_partner.reset();
}

// Takes a piece off a shown list, so a second press before the next refresh cannot move it twice.
bool claim(std::vector<game::Cargo>& shown, uint64_t handle) {
    const auto piece = std::find_if(shown.begin(), shown.end(), [&](const auto& c) { return c.handle == handle; });
    if (piece == shown.end()) return false;
    shown.erase(piece);
    return true;
}

void runRequests(NetClient& net) {
    std::vector<Request> requests;
    uint8_t partnerSlot = 0;
    {
        std::lock_guard lock(g_mutex);
        if (!g_partner) {
            g_requests.clear();
            return;
        }
        for (const Request& request : g_requests) {
            auto& shown = request.action == Action::Give ? g_local : g_partner->cargo;
            if (claim(shown, request.piece.handle)) requests.push_back(request);
        }
        g_requests.clear();
        partnerSlot = g_partner->slot;
    }
    for (const Request& request : requests) {
        if (request.action == Action::Give) {
            if (!game::removeCargo(request.piece.handle)) continue;
            cargo_transfer::sendPiece(net, partnerSlot, request.piece);
            logger::write("cargo: gave %s (%u) to the guest", request.piece.name.c_str(), request.piece.type);
        } else {
            g_awaited.push_back(request.piece.type);
            net.send(cargo_transfer::kMsgCargoTake, true, partnerSlot,
                     proto::bytesOf(cargo_transfer::CargoTake{request.piece.handle}));
        }
    }
}

}  // namespace

namespace cargo_transfer {

void sendPiece(NetClient& net, uint8_t slot, const game::Cargo& piece) {
    const CargoAdd add{piece.type, piece.category, {}, piece.durability, 0, piece.orderId, piece.secondId};
    net.send(kMsgCargoAdd, true, slot, proto::bytesOf(add));
}

void onFrame(NetClient& net, const GameFrame& frame) {
    const bool host = g_host.load();
    if (!host && frame.slot != g_hostSlot.load()) return;  // a guest only takes orders from the host
    if (frame.type == kMsgCargoList) {
        if (auto partner = parseList(frame.slot, frame.payload)) {
            std::lock_guard lock(g_mutex);
            g_partner = std::move(partner);
        }
    } else if (frame.type == kMsgCargoTake && !host && frame.payload.size() == sizeof(CargoTake)) {
        CargoTake request;
        std::memcpy(&request, frame.payload.data(), sizeof(request));
        giveUp(net, frame.slot, request.handle);
    } else if (frame.type == kMsgCargoAdd && frame.payload.size() == sizeof(CargoAdd)) {
        CargoAdd add;
        std::memcpy(&add, frame.payload.data(), sizeof(add));
        if (host) {
            received(pieceOf(add));
        } else {
            receive(pieceOf(add));
            logger::write("cargo: received %u from the host (order %llx)", add.type,
                          static_cast<unsigned long long>(add.orderId));
        }
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const bool host = session.linked && session.localSlot == session.hostSlot;
    g_host = host;
    g_hostSlot = session.hostSlot;
    const auto now = Clock::now();
    refreshLocal(now);
    retryPendingAdds(now);
    forgetDepartedPartner(session);
    if (!session.linked) return;
    report(net, now);
    if (host) runRequests(net);
}

bool isHost() { return g_host.load(); }

std::vector<game::Cargo> localCargo() {
    std::lock_guard lock(g_mutex);
    return g_local;
}

std::optional<Partner> partner() {
    std::lock_guard lock(g_mutex);
    return g_partner;
}

void give(const game::Cargo& piece) {
    std::lock_guard lock(g_mutex);
    g_requests.push_back({Action::Give, piece});
}

void take(const game::Cargo& piece) {
    std::lock_guard lock(g_mutex);
    g_requests.push_back({Action::Take, piece});
}

}  // namespace cargo_transfer
