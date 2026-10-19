#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

// The pure rules of the ambient population for 3 or more players (milestone 9, unit tested; docs/SR2_NOTES.md,
// "Population"). 2-player co-op keeps the world at single-player density in two ways: the per-machine budgets are halved
// while the partner is near and grow to the full single-player value as it walks away, and only the host spawns for a pair
// that stands together. For 3 or more players the budgets stop at what the game gives two players apart (milestone 12: the
// pools are sized for that), so with 3 or 4 separate areas each has a share of it. With one remote every rule gives the
// answer of the shipped game.
namespace population_rules {

struct Point {
    float x, y, z;
};

constexpr int32_t kVehicleHeadroomPerRemote = 4;
constexpr size_t kBudgetRemotesAtLeast = 2;  // below this the game's own function answers
constexpr float kMostExtraAreas = 1.0f;      // the whole base is the 2-player session total: more areas thin the density

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

// The budget of `base` for the given number of extra areas: half the base for the local area, and up to half more for the
// others (the shipped function truncates the fractional part the same way). Every machine holds every replicated object of
// the session, so `base`, the shipped maximum for 2 players apart, is also the session total: with three or four areas it
// is shared out (base/3, base/4 per area), and the pools (150 humans, 32 vehicles) are never asked for more than the game
// asks for two players.
inline uint32_t budget(uint32_t base, float extra) {
    const uint32_t half = base >> 1;
    return half + static_cast<uint32_t>(static_cast<float>(half) * std::min(extra, kMostExtraAreas));
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

// The vehicles this machine may still add: the shipped function takes 4 off while a partner exists, here 4 per remote
// session member. Members, not humans: a joiner counts the session's members from the join on, so it keeps room for the
// other players' vehicles from its first spawn, before any remote human exists on it.
inline int32_t vehicleHeadroom(int32_t free, size_t remoteMembers) {
    return free - kVehicleHeadroomPerRemote * static_cast<int32_t>(remoteMembers);
}

}  // namespace population_rules
