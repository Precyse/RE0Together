#include "cargo_ground.h"

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
constexpr auto kRemember = std::chrono::seconds(10);  // a piece passes through the hands between ground and backpack
constexpr double kWatchMetres = 8.0;  // loose pieces this close are watched (a pickup reaches about 2 m)
constexpr double kMatchMetres = 3.0;  // the other world's copy is looked for this far around the reported spot
constexpr double kPlaceMetres = 300.0;  // a partner's drop is placed once this player is this close (terrain loaded)
constexpr float kToastSeconds = 4.0f;

struct SeenLoose {
    game::LooseCargo piece;
    Clock::time_point at;
};

std::atomic<bool> g_host{false};
std::atomic<uint8_t> g_hostSlot{0};

// Net thread only.
Clock::time_point g_lastWatch;
std::map<uint64_t, SeenLoose> g_recentLoose;            // loose pieces seen around the player lately
std::map<uint64_t, Clock::time_point> g_recentCarried;  // pieces seen in the backpack lately
std::set<uint64_t> g_carried;                           // handles in the backpack at the last watch
std::map<uint32_t, game::Cargo> g_pending;              // guest: pickups waiting for the host, by request number
std::vector<cargo_ground::Spot> g_farDrops;             // partner drops waiting until this player comes near
uint32_t g_nextRequest = 1;

double distanceSquared(const world_to_screen::Vec3& a, const world_to_screen::Vec3& b) {
    const world_to_screen::Vec3 d = a - b;
    return world_to_screen::dot(d, d);
}

cargo_ground::Spot spot(uint32_t request, uint32_t type, const world_to_screen::Vec3& at, uint64_t orderId) {
    return {request, type, {static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z)}, 0, orderId};
}

world_to_screen::Vec3 where(const cargo_ground::Spot& spot) {
    return {spot.position[0], spot.position[1], spot.position[2]};
}

// The loose piece of this kind lying nearest the spot in this world, within kMatchMetres.
std::optional<uint64_t> findLoose(uint32_t type, const world_to_screen::Vec3& at, uint64_t orderId) {
    if (orderId) return game::findOrderPiece(orderId);  // identity: wherever this world keeps it
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
    const cargo_ground::Spot request = spot(g_nextRequest++, piece.type, at, piece.orderId);
    if (!net.send(cargo_ground::kMsgPickup, true, hostSlot, proto::bytesOf(request))) return;
    g_pending[request.request] = piece;
    logger::write("cargo_ground: asked the host for %s (%u)", piece.name.c_str(), piece.type);
}

void announce(NetClient& net, uint16_t type, const char* what, uint32_t kind, const world_to_screen::Vec3& at,
              uint64_t orderId) {
    net.send(type, true, proto::kSlotAll, proto::bytesOf(spot(0, kind, at, orderId)));
    logger::write("cargo_ground: %s %u at (%.1f, %.1f, %.1f)", what, kind, at.x, at.y, at.z);
}

// Compares the backpack and the loose pieces around the player with what was seen lately: a piece seen loose that is
// now in the backpack was picked up (the guest asks the host, the host tells the guests); one seen in the backpack that
// is now loose was put down (the other world gets the same piece there). A piece passes through the player's hands on
// the way, so "lately" spans kRemember rather than one watch.
void watch(NetClient& net, const SessionSnapshot& session, bool host, Clock::time_point now) {
    const auto player = game::localPlayer();
    if (!player) return;
    for (const game::LooseCargo& piece : game::looseCargo(player->position, kWatchMetres)) {
        const bool newlyLoose = !g_recentLoose.contains(piece.handle);
        g_recentLoose[piece.handle] = {piece, now};
        if (newlyLoose && g_recentCarried.erase(piece.handle)) {
            announce(net, cargo_ground::kMsgDrop, "put down", piece.type, piece.position, piece.orderId);
        }
    }
    std::set<uint64_t> carriedNow;
    for (const game::Cargo& piece : game::carriedCargo()) {
        carriedNow.insert(piece.handle);
        g_recentCarried[piece.handle] = now;
        const auto loose = g_recentLoose.find(piece.handle);
        if (g_carried.contains(piece.handle) || loose == g_recentLoose.end()) continue;
        const world_to_screen::Vec3 at = loose->second.piece.position;
        g_recentLoose.erase(loose);
        if (host) {
            announce(net, cargo_ground::kMsgHostPickup, "host picked up", piece.type, at, piece.orderId);
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
void answer(NetClient& net, uint8_t guestSlot, const cargo_ground::Spot& request) {
    const auto handle = findLoose(request.type, where(request), request.orderId);
    const bool accepted = handle && game::removeCargo(*handle);
    net.send(cargo_ground::kMsgPickupResult, true, guestSlot,
             proto::bytesOf(cargo_ground::PickupResult{request.request, accepted ? 1u : 0u}));
    logger::write("cargo_ground: guest picked up %u at (%.1f, %.1f, %.1f): %s", request.type, request.position[0],
                  request.position[1], request.position[2], accepted ? "accepted" : "refused");
}

// Guest: the host picked up a piece, so this world's copy goes too.
void follow(const cargo_ground::Spot& pickup) {
    const auto handle = findLoose(pickup.type, where(pickup), pickup.orderId);
    if (handle) game::removeCargo(*handle);
    logger::write("cargo_ground: host picked up %u at (%.1f, %.1f, %.1f), %s here", pickup.type, pickup.position[0],
                  pickup.position[1], pickup.position[2], handle ? "removed" : "not found");
}

// The partner's drops appear here once this player is near enough for the ground there to be loaded (a piece placed
// over unloaded terrain falls through it).
void placeNearDrops() {
    const auto player = game::localPlayer();
    if (!player || g_farDrops.empty()) return;
    std::erase_if(g_farDrops, [&](const cargo_ground::Spot& drop) {
        if (distanceSquared(where(drop), player->position) > kPlaceMetres * kPlaceMetres) return false;
        const bool placed = game::placeCargo(drop.type, where(drop));
        logger::write("cargo_ground: partner put down %u at (%.1f, %.1f, %.1f), %s here", drop.type, drop.position[0],
                      drop.position[1], drop.position[2], placed ? "placed" : "not placed");
        return true;
    });
}

// Guest: a refused piece leaves the backpack again.
void settle(const cargo_ground::PickupResult& result) {
    const auto pending = g_pending.find(result.request);
    if (pending == g_pending.end()) return;
    const game::Cargo piece = pending->second;
    g_pending.erase(pending);
    if (result.accepted) return;
    game::removeCargo(piece.handle);
    toast_queue::push(("Pickup refused by the host: " + piece.name).c_str(), kToastSeconds);
    logger::write("cargo_ground: host refused %s (%u)", piece.name.c_str(), piece.type);
}

template <class T>
std::optional<T> payloadAs(const GameFrame& frame) {
    if (frame.payload.size() != sizeof(T)) return std::nullopt;
    T value;
    std::memcpy(&value, frame.payload.data(), sizeof(value));
    return value;
}

}  // namespace

namespace cargo_ground {

void onFrame(NetClient& net, const GameFrame& frame) {
    const bool host = g_host.load();
    if (frame.type == kMsgDrop) {
        if (const auto drop = payloadAs<Spot>(frame)) g_farDrops.push_back(*drop);
        return;
    }
    if (frame.type == kMsgPickup && host) {
        if (const auto request = payloadAs<Spot>(frame)) answer(net, frame.slot, *request);
        return;
    }
    if (host || frame.slot != g_hostSlot.load()) return;  // the rest comes from the host only
    if (frame.type == kMsgPickupResult) {
        if (const auto result = payloadAs<PickupResult>(frame)) settle(*result);
    } else if (frame.type == kMsgHostPickup) {
        if (const auto pickup = payloadAs<Spot>(frame)) follow(*pickup);
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const bool host = session.linked && session.localSlot == session.hostSlot;
    g_host = host;
    g_hostSlot = session.hostSlot;
    const auto now = Clock::now();
    if (!session.linked || now - g_lastWatch < kWatchInterval) return;
    g_lastWatch = now;
    placeNearDrops();
    watch(net, session, host, now);
}

}  // namespace cargo_ground
