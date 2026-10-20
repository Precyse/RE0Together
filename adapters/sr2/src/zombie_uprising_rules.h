#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

#include "partner_rules.h"
#include "sr2_addresses.h"

// The pure rules of Zombie Uprising for 3 or 4 players (milestone 17 wave 3, unit tested; docs/SR2_NOTES.md, "Zombie Uprising for 3 to 4
// players (milestone 17 wave 3)"): where each remote starts, the health the wave bonus averages and the players the spawn picker tests.
namespace zombie_uprising_rules {

constexpr size_t kSpawnPlayerSlots = 12;  // the players the picker's view and distance loops walk
constexpr size_t kSpawnPairSlots = 2;     // the players the game's own array holds: the local player and the first remote
constexpr size_t kExtraSlots = kSpawnPlayerSlots - kSpawnPairSlots;
static_assert(kSpawnPlayerSlots == sr2::kMaxWalkedPlayers);

// Where every remote is placed at the wave start: the shipped partner spot, `partnerOffset` along x from the marker. Players placed on
// one spot are spread by MC5 (spread_rules, by guest rank), so the spot is the same for all.
inline partner_rules::Vec3 startSpot(partner_rules::Vec3 marker, float partnerOffset) {
    return {marker.x + partnerOffset, marker.y, marker.z};
}

// A human's health as a fraction of its maximum, 0 without a maximum.
inline float healthFraction(float health, int32_t maxHealth) { return maxHealth > 0 ? health / static_cast<float>(maxHealth) : 0.0f; }

// The mean of the local fraction and the remotes'; the local one alone without remotes. With one remote it is the shipped (l + p) * 0.5.
inline float averageHealth(float local, std::span<const float> remotes) {
    float sum = local;
    for (const float fraction : remotes) sum += fraction;
    return sum / static_cast<float>(1 + remotes.size());
}

// "0.500 1.000" for the trace lines.
inline std::string fractionsText(std::span<const float> fractions) {
    std::string text;
    for (size_t i = 0; i < fractions.size(); ++i) {
        char part[24];
        std::snprintf(part, sizeof(part), "%s%.3f", i == 0 ? "" : " ", static_cast<double>(fractions[i]));
        text += part;
    }
    return text;
}

// The players of the picker beyond the game's pair of local player and first remote.
struct SpawnPlayers {
    uint32_t extras[kExtraSlots];
    size_t extraCount;
};

// The remotes after the first of `walk` (the remote humans in list order), as many as fit.
inline SpawnPlayers spawnExtrasOf(const uintptr_t* walk, size_t count) {
    SpawnPlayers players{};
    for (size_t i = 1; i < count && players.extraCount < kExtraSlots; ++i) players.extras[players.extraCount++] = static_cast<uint32_t>(walk[i]);
    return players;
}

// The player the loops test at `index`: the game's pair first, then the extras, 0 (nobody) past them.
inline uint32_t spawnPlayerAt(const uint32_t* pair, const SpawnPlayers& extras, uint32_t index) {
    if (index < kSpawnPairSlots) return pair[index];
    const size_t extra = index - kSpawnPairSlots;
    return extra < extras.extraCount ? extras.extras[extra] : 0;
}

}  // namespace zombie_uprising_rules
