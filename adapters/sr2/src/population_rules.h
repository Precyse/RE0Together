#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "sr2_addresses.h"

// The pure rules of the ambient population for 3 or more players (milestone 9, unit tested; docs/SR2_NOTES.md,
// "Population"). 2-player co-op keeps the world at single-player density in two ways: the per-machine budgets are halved
// while the partner is near and grow to the full single-player value as it walks away, and only the host spawns for a pair
// that stands together. The invariant is "every player area is at single-player density"; these rules keep it for any
// number of players. With one remote every rule gives the answer of the shipped game.
namespace population_rules {

struct Point {
    float x, y, z;
};

constexpr uint32_t kNpcBudgetBase = 0x96;    // the total humans budget (single player and multiplayer use 0x4B)
constexpr uint32_t kNpcPoolCapacity = sr2::kNpcHumanPool;  // humans the static NPC pool holds
constexpr int32_t kVehicleHeadroomPerRemote = 4;
constexpr size_t kBudgetRemotesAtLeast
 = 2;  // below this the game's own function answers

inline float distance(const Point& a, const Point& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// How far apart two players' areas are: 0 within `radius`, 1 from twice the radius on, linear between (the shipped budget).
inline float farness(float apart, float radius) { return std::clamp((apart - radius) / radius, 0.0f, 1.0f); }

// The soft number of separate player areas beyond the local player's own. The remotes are taken in order: remote i adds
// how far it is from the local player, but no more than how far it is from any remote before it (a player who stands with
// one of them is not a new area). Together it is 0, all apart one per remote, two remotes together far from the local
// player 1. With one remote it is farness(distance, radius), the shipped budget's own fraction.
inline float extraAreas(const Point& local, const Point* remotes, size_t count, float radius) {
    float extra = 0;
    for (size_t i = 0; i < count; ++i) {
        float newness = farness(distance(local, remotes[i]), radius);
        for (size_t j = 0; j < i; ++j) newness = std::min(newness, farness(distance(remotes[j], remotes[i]), radius));
        extra += newness;
    }
    return extra;
}

// The player areas the census reports: the local one plus the extra ones, in hundredths.
constexpr uint32_t kAreaHundredths = 100;
inline uint32_t areasHundredths(float extra) {
    return static_cast<uint32_t>((1.0f + extra) * static_cast<float>(kAreaHundredths) + 0.5f);
}

// The budget of `base` for the given number of extra areas: half the base for the local area, half more for each (the
// shipped function truncates the fractional part the same way). The humans budget never exceeds what the NPC pool holds.
inline uint32_t budget(uint32_t base, float extra) {
    const uint32_t half = base >> 1;
    const uint32_t result = half + static_cast<uint32_t>(static_cast<float>(half) * extra);
    return base == kNpcBudgetBase ? std::min(result, kNpcPoolCapacity) : result;
}

// Another player with the owner slot of its machine, for the cluster rule.
struct Peer {
    uint8_t slot;
    Point position;
};

// The ambient spawner's cluster test (0x957A40). The shipped function answers 1 ("far") when the partner is not within
// `radius` and this machine spawns around itself; it answers 0 while the partner is near, and then a guest does not spawn
// and the host spawns for both. With 3 or more players the lowest slot of a cluster is the one that spawns for it: the
// host is near to any player in range, a guest only to a player in range with a lower slot (so the lowest guest of a
// cluster far from the host still spawns). With one remote this is the shipped answer (a guest's one remote is the host,
// slot 0).
inline bool clusterFar(bool localIsHost, uint8_t localSlot, const Point& local, const Peer* peers, size_t count,
                       float radius) {

    for (size_t i = 0; i < count; ++i) {
        if (distance(local, peers[i].position) >= radius) continue;
        if (localIsHost || peers[i].slot < localSlot) return false;
    }
    return true;
}

// The vehicles this machine may still add: the shipped function takes 4 off while a partner exists, here 4 per remote.
inline int32_t vehicleHeadroom(int32_t free, size_t remotes) {
    return free - kVehicleHeadroomPerRemote * static_cast<int32_t>(remotes);
}

}  // namespace population_rules
