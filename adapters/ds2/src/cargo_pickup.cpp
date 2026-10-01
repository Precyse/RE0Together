#include "cargo_pickup.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <optional>
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
constexpr double kMatchMetres = 3.0;  // the other world's copy is looked for this far around the reported spot
constexpr auto kRemember = std::chrono::seconds(10);  // a piece passes through the hands between ground and backpack
constexpr float kToastSeconds = 4.0f;

std::atomic<bool> g_host{false};
std::atomic<uint8_t> g_hostSlot{0};

struct SeenLoose {
    game::LooseCargo piece;
    Clock::time_point at;
};

// Net thread only.
Clock::time_point g_lastWatch;
std::map<uint64_t, SeenLoose> g_recentLoose;            // loose pieces seen around the player lately
std::map<uint64_t, Clock::time_point> g_recentCarried;  // pieces seen in the backpack lately
std::set<uint64_t> g_carried;                           // handles in the backpack at the last watch
std::set<uint64_t> g_ownDrops;                          // pieces this player put down itself (picked up again freely)
std::map<uint32_t, game::Cargo> g_pending;              // guest: pickups waiting for the host, by request number
uint32_t g_nextRequest = 1;

double distanceSquared(const world_to_screen::Vec3& a, const world_to_screen::Vec3& b) {
    const world_to_screen::Vec3 d = a - b;
    return world_to_screen::dot(d, d);
}

cargo_pickup::Pickup message(uint32_t request, uint32_t type, const world_to_screen::Vec3& at) {
    return {request, type, {static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z)}};
}

world_to_screen::Vec3 spot(const cargo_pickup::Pickup& pickup) {
    return {pickup.position[0], pickup.position[1], pickup.position[2]};
}

// The loose piece of this kind lying nearest the spot in this world, within kMatchMetres.
std::optional<uint64_t> findLoose(uint32_t type, const world_to_screen::Vec3& at) {
    std::optional<uint64_t> best;
    double bestDistance = 0;
    for (const game::LooseCargo& piece : game::looseCargo(at, kMatchMetres)) {
        const double distance = distanceSquared(piece.position, at);
        if (piece.type != type || (best && distance >= bestDistance)) continue;
        best = piece.handle;
        bestDistance = distance;
    }
    return best;
}

void ask(NetClient& net, uint8_t hostSlot, const game::Cargo& piece, const world_to_screen::Vec3& at) {
    const cargo_pickup::Pickup request = message(g_nextRequest++, piece.type, at);
    if (!net.send(cargo_pickup::kMsgPickup, true, hostSlot, proto::bytesOf(request))) return;
    g_pending[request.request] = piece;
    logger::write("cargo_pickup: asked the host for %s (%u)", piece.name.c_str(), piece.type);
}

void announce(NetClient& net, const game::Cargo& piece, const world_to_screen::Vec3& at) {
    net.send(cargo_pickup::kMsgHostPickup, true, proto::kSlotAll, proto::bytesOf(message(0, piece.type, at)));
    logger::write("cargo_pickup: host picked up %s (%u)", piece.name.c_str(), piece.type);
}

// Compares the backpack and the loose pieces around the player with what was seen lately: a piece seen loose that is
// now in the backpack was picked up (the guest asks the host, the host tells the guests); one seen in the backpack that
// is now loose was put down by this player. A piece passes through the player's hands on the way, so "lately" spans
// kRemember rather than one watch.
void watch(NetClient& net, const SessionSnapshot& session, bool host, Clock::time_point now) {
    const auto player = game::localPlayer();
    if (!player) return;
    for (const game::LooseCargo& piece : game::looseCargo(player->position, kWatchMetres)) {
        const bool newlyLoose = !g_recentLoose.contains(piece.handle);
        if (newlyLoose && g_recentCarried.contains(piece.handle)) g_ownDrops.insert(piece.handle);
        g_recentLoose[piece.handle] = {piece, now};
    }
    std::set<uint64_t> carriedNow;
    for (const game::Cargo& piece : game::carriedCargo()) {
        carriedNow.insert(piece.handle);
        g_recentCarried[piece.handle] = now;
        const auto loose = g_recentLoose.find(piece.handle);
        if (g_carried.contains(piece.handle) || loose == g_recentLoose.end()) continue;
        const world_to_screen::Vec3 at = loose->second.piece.position;
        g_recentLoose.erase(loose);
        if (g_ownDrops.erase(piece.handle)) continue;
        if (host) {
            announce(net, piece, at);
        } else {
            ask(net, session.hostSlot, piece, at);
        }
    }
    g_carried = std::move(carriedNow);
    const Clock::time_point cutoff = now - kRemember;
    std::erase_if(g_recentLoose, [cutoff](const auto& seen) { return seen.second.at < cutoff; });
    std::erase_if(g_recentCarried, [cutoff](const auto& seen) { return seen.second < cutoff; });
}

// Host: takes the guest's piece out of this world if it lies here too.
void answer(NetClient& net, uint8_t guestSlot, const cargo_pickup::Pickup& request) {
    const auto handle = findLoose(request.type, spot(request));
    const bool accepted = handle && game::removeCargo(*handle);
    net.send(cargo_pickup::kMsgPickupResult, true, guestSlot,
             proto::bytesOf(cargo_pickup::PickupResult{request.request, accepted ? 1u : 0u}));
    logger::write("cargo_pickup: guest picked up %u at (%.1f, %.1f, %.1f): %s", request.type, request.position[0],
                  request.position[1], request.position[2], accepted ? "accepted" : "refused");
}

// Guest: the host picked up a piece, so this world's copy goes too.
void follow(const cargo_pickup::Pickup& pickup) {
    const auto handle = findLoose(pickup.type, spot(pickup));
    if (handle) game::removeCargo(*handle);
    logger::write("cargo_pickup: host picked up %u at (%.1f, %.1f, %.1f), %s here", pickup.type, pickup.position[0],
                  pickup.position[1], pickup.position[2], handle ? "removed" : "not found");
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
    const bool host = g_host.load();
    if (frame.type == kMsgPickup && host && frame.payload.size() == sizeof(Pickup)) {
        Pickup request;
        std::memcpy(&request, frame.payload.data(), sizeof(request));
        answer(net, frame.slot, request);
        return;
    }
    if (host || frame.slot != g_hostSlot.load()) return;  // the rest comes from the host only
    if (frame.type == kMsgPickupResult && frame.payload.size() == sizeof(PickupResult)) {
        PickupResult result;
        std::memcpy(&result, frame.payload.data(), sizeof(result));
        settle(result);
    } else if (frame.type == kMsgHostPickup && frame.payload.size() == sizeof(Pickup)) {
        Pickup pickup;
        std::memcpy(&pickup, frame.payload.data(), sizeof(pickup));
        follow(pickup);
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const bool host = session.linked && session.localSlot == session.hostSlot;
    g_host = host;
    g_hostSlot = session.hostSlot;
    const auto now = Clock::now();
    if (!session.linked || now - g_lastWatch < kWatchInterval) return;
    g_lastWatch = now;
    watch(net, session, host, now);
}

}  // namespace cargo_pickup
