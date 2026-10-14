#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

// The pure rules of players placed on one spot (milestone 9 MC3, MC5, unit tested). The end position of a cutscene and a
// script's teleport carry one position for "the partner"; with three or four players they would all land on it. Each guest
// is moved a step along the right-hand direction of the placement, the lowest guest (player 2) not at all, so the shipped
// placement is what player 2 gets.
namespace spread_rules {

constexpr float kPlayerSpread = 1.5f;  // between two players on one spot, about two body widths
constexpr float kMinLength = 1e-4f;    // below this the direction is not one

struct Offset {
    float x, z;
};

// The number of guests with a lower slot: 0 for the host and for the lowest guest. `slots` are the session slots of all
// members, the host's included.
inline uint32_t guestRank(uint8_t slot, uint8_t hostSlot, const uint8_t* slots, size_t count) {
    if (slot == hostSlot) return 0;
    uint32_t rank = 0;
    for (size_t i = 0; i < count; ++i) {
        if (slots[i] < slot && slots[i] != hostSlot) ++rank;
    }
    return rank;
}

// `rank` steps along the horizontal direction (x, z); the x axis when it has no length.
inline Offset stepsAlong(uint32_t rank, float x, float z) {
    const float length = std::sqrt(x * x + z * z);
    const float scale = rank * kPlayerSpread;
    if (length < kMinLength) return {scale, 0};
    return {x / length * scale, z / length * scale};
}

// The direction to a placement's right from its forward vector (y up), as the horizontal pair the steps go along.
inline Offset rightOf(float forwardX, float forwardZ) { return {forwardZ, -forwardX}; }

}  // namespace spread_rules
