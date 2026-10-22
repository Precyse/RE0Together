#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

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
inline uint32_t budgetUncapped(uint32_t base, float extra) {
    const uint32_t half = base >> 1;
    return half + static_cast<uint32_t>(static_cast<float>(half) * extra);
}

inline uint32_t budget(uint32_t base, float extra) { return budgetUncapped(base, std::min(extra, kMostExtraAreas)); }

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

// The vehicles this machine may still add for the pool: 4 are kept per remote session member for the other players' vehicles,
// the shipped function keeps 4 while a partner exists. Members, not humans: a joiner counts the session's members from the
// join on, so it keeps room for the other players' vehicles from its first spawn, before any remote human exists on it. The
// pool is `kShippedVehiclePool` while no remote member exists, whatever its size (the 64 vehicle pool is for sessions).
constexpr int32_t kShippedVehiclePool = 32;

inline int32_t poolHeadroom(int32_t capacity, int32_t vehiclesInUse, size_t remoteMembers) {
    const int32_t usable = remoteMembers == 0 ? kShippedVehiclePool : capacity;
    return usable - vehiclesInUse - kVehicleHeadroomPerRemote * static_cast<int32_t>(remoteMembers);
}

// A player with a human, for the traffic quotas: the owner slot of its machine and its position.
struct Player {
    uint8_t slot;
    Point position;
};

constexpr size_t kMaxTrafficPlayers = 12;  // the session is built for 12 players

// The slot that leads each player's cluster: the lowest slot among the players within `radius` of it, itself included (the
// cluster rule W2a for every player, not only the local one). leaders[i] belongs to players[i].
inline void trafficLeaders(const Player* players, size_t count, float radius, uint8_t* leaders) {
    for (size_t i = 0; i < count; ++i) {
        leaders[i] = players[i].slot;
        for (size_t j = 0; j < count; ++j) {
            if (distance(players[i].position, players[j].position) < radius && players[j].slot < leaders[i]) leaders[i] = players[j].slot;
        }
    }
}

// What the local machine may own of the ambient traffic: its share of the vehicle cap `limit`, how many players it leads and
// of how many there are, and their slots (the local one among them when it leads itself).
struct Quota {
    int32_t quota = 0;
    size_t leads = 0;
    size_t players = 0;
    uint8_t led[kMaxTrafficPlayers] = {};
};

// A leader of k of N players may own floor(limit * k / N) traffic vehicles; the remainder goes one each to the leaders in
// ascending slot order, so the shares add up to the limit. A non-leader gets 0. With fewer than two players there is nothing to
// share and the whole limit is the quota.
inline Quota trafficQuota(int32_t limit, const Player* players, const uint8_t* leaders, size_t count, uint8_t localSlot) {
    Quota result;
    result.players = count;
    uint8_t slots[kMaxTrafficPlayers];
    size_t led[kMaxTrafficPlayers] = {};
    size_t leaderCount = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t at = 0;
        while (at < leaderCount && slots[at] != leaders[i]) ++at;
        if (at == leaderCount) slots[leaderCount++] = leaders[i];
        ++led[at];
        if (leaders[i] == localSlot) result.led[result.leads++] = players[i].slot;
    }
    if (count < 2) {
        result.quota = limit;
        return result;
    }
    for (size_t a = 1; a < leaderCount; ++a) {  // ascending slot order
        for (size_t b = a; b > 0 && slots[b] < slots[b - 1]; --b) {
            std::swap(slots[b], slots[b - 1]);
            std::swap(led[b], led[b - 1]);
        }
    }
    int32_t shares[kMaxTrafficPlayers];
    int32_t given = 0;
    for (size_t k = 0; k < leaderCount; ++k) {
        shares[k] = static_cast<int32_t>(static_cast<int64_t>(limit) * static_cast<int64_t>(led[k]) / static_cast<int64_t>(count));
        given += shares[k];
    }
    for (size_t k = 0; k < leaderCount && given < limit; ++k, ++given) ++shares[k];
    for (size_t k = 0; k < leaderCount; ++k) {
        if (slots[k] == localSlot) result.quota = shares[k];
    }
    return result;
}

// The vehicles this machine may add: the pool rule, and with two or more remote members also what is left of the traffic quota
// (owned traffic counts against it). With one remote member or none it is the pool rule alone, which is the shipped function's
// answer for the shipped pool.
inline int32_t vehicleHeadroom(int32_t capacity, int32_t vehiclesInUse, size_t remoteMembers, int32_t quota, int32_t ownedTraffic) {
    const int32_t pool = poolHeadroom(capacity, vehiclesInUse, remoteMembers);
    return remoteMembers <= 1 ? pool : std::min(pool, quota - ownedTraffic);
}

}  // namespace population_rules
