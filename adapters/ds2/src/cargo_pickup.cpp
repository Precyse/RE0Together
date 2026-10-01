#include "cargo_pickup.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "game.h"
#include "log.h"
#include "toast_queue.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kWatchInterval = std::chrono::milliseconds(500);
constexpr double kWatchMetres = 8.0;  // loose pieces this close are watched (a pickup reaches about 2 m)
constexpr double kMatchMetres = 3.0;  // the host looks this far around the reported spot for the same kind
constexpr float kToastSeconds = 4.0f;

std::atomic<bool> g_host{false};
std::atomic<uint8_t> g_hostSlot{0};

// Guest, net thread only.
Clock::time_point g_lastWatch;
std::map<uint64_t, game::LooseCargo> g_nearby;  // loose pieces around the player at the last watch
std::set<uint64_t> g_carried;                   // handles in the backpack at the last watch
std::set<uint64_t> g_ownDrops;                  // pieces the guest put down itself (picked up again freely)
std::map<uint32_t, game::Cargo> g_pending;      // pickups waiting for the host, by request number
uint32_t g_nextRequest = 1;

double distanceSquared(const world_to_screen::Vec3& a, const world_to_screen::Vec3& b) {
    const world_to_screen::Vec3 d = a - b;
    return world_to_screen::dot(d, d);
}

void ask(NetClient& net, uint8_t hostSlot, const game::Cargo& piece, const world_to_screen::Vec3& at) {
    const cargo_pickup::Pickup request{g_nextRequest++, piece.type,
                                       {static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z)}};
    if (!net.send(cargo_pickup::kMsgPickup, true, hostSlot, proto::bytesOf(request))) return;
    g_pending[request.request] = piece;
    logger::write("cargo_pickup: asked the host for %s (%u)", piece.name.c_str(), piece.type);
}

// Compares the backpack and the loose pieces around the player with the previous watch: a piece that was loose and
// is now carried was picked up; one that was carried and is now loose was put down by us.
void watch(NetClient& net, const SessionSnapshot& session) {
    const auto player = game::localPlayer();
    if (!player) return;
    const std::vector<game::Cargo> carried = game::carriedCargo();
    std::map<uint64_t, game::LooseCargo> nearby;
    for (const game::LooseCargo& piece : game::looseCargo(player->position, kWatchMetres)) nearby[piece.handle] = piece;

    std::set<uint64_t> carriedNow;
    for (const game::Cargo& piece : carried) {
        carriedNow.insert(piece.handle);
        const auto wasLoose = g_nearby.find(piece.handle);
        if (g_carried.contains(piece.handle) || wasLoose == g_nearby.end()) continue;
        if (!g_ownDrops.erase(piece.handle)) ask(net, session.hostSlot, piece, wasLoose->second.position);
    }
    for (uint64_t handle : g_carried) {
        if (!carriedNow.contains(handle) && nearby.contains(handle)) g_ownDrops.insert(handle);
    }
    g_carried = std::move(carriedNow);
    g_nearby = std::move(nearby);
}

// Host: takes the guest's piece out of this world if it lies here too.
void answer(NetClient& net, uint8_t guestSlot, const cargo_pickup::Pickup& request) {
    const world_to_screen::Vec3 at{request.position[0], request.position[1], request.position[2]};
    const std::vector<game::LooseCargo> candidates = game::looseCargo(at, kMatchMetres);
    const auto nearest = std::min_element(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
        const bool aMatches = a.type == request.type, bMatches = b.type == request.type;
        if (aMatches != bMatches) return aMatches;
        return distanceSquared(a.position, at) < distanceSquared(b.position, at);
    });
    const bool accepted = nearest != candidates.end() && nearest->type == request.type && game::removeCargo(nearest->handle);
    net.send(cargo_pickup::kMsgPickupResult, true, guestSlot,
             proto::bytesOf(cargo_pickup::PickupResult{request.request, accepted ? 1u : 0u}));
    logger::write("cargo_pickup: guest picked up %u at (%.1f, %.1f, %.1f): %s", request.type, at.x, at.y, at.z,
                  accepted ? "accepted" : "refused");
}

// Guest: a refused piece leaves the backpack again.
void settle(const cargo_pickup::PickupResult& result) {
    const auto pending = g_pending.find(result.request);
    if (pending == g_pending.end()) return;
    const game::Cargo piece = pending->second;
    g_pending.erase(pending);
    if (result.accepted) return;
    game::removeCargo(piece.handle);
    toast_queue::push(("Pickup refused by the host: " + piece.name).c_str(), kToastSeconds);
    logger::write("cargo_pickup: host refused %s (%u)", piece.name.c_str(), piece.type);
}

}  // namespace

namespace cargo_pickup {

void onFrame(NetClient& net, const GameFrame& frame) {
    if (frame.type == kMsgPickup && g_host.load() && frame.payload.size() == sizeof(Pickup)) {
        Pickup request;
        std::memcpy(&request, frame.payload.data(), sizeof(request));
        answer(net, frame.slot, request);
    } else if (frame.type == kMsgPickupResult && !g_host.load() && frame.slot == g_hostSlot.load() &&
               frame.payload.size() == sizeof(PickupResult)) {
        PickupResult result;
        std::memcpy(&result, frame.payload.data(), sizeof(result));
        settle(result);
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const bool host = session.linked && session.localSlot == session.hostSlot;
    g_host = host;
    g_hostSlot = session.hostSlot;
    const auto now = Clock::now();
    if (!session.linked || host || now - g_lastWatch < kWatchInterval) return;
    g_lastWatch = now;
    watch(net, session);
}

}  // namespace cargo_pickup
