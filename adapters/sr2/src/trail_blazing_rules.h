#pragma once
#include <cstdint>

// The pure rules of Trail Blazing for 3 or 4 players (docs/SR2_NOTES.md, "Trail Blazing (milestone 17 wave 3)"); no game reads.
namespace trail_blazing_rules {

constexpr float kWaterDepthLimit = 1.0f;  // the fld1 the game compares the submerged depth with
constexpr uint8_t kSubmergedBit = 1;      // bit 0 of the human's flag byte at +0x84

// True when the human is in water: swimming, or submerged deeper than the limit. A depth of exactly the limit and NaN are not.
constexpr bool inWater(int32_t swimming, uint8_t flags, float depth) {
    return swimming != 0 || ((flags & kSubmergedBit) != 0 && kWaterDepthLimit < depth);
}

// The smaller of the two squared distances the checkpoint test goes on with: a candidate equal to the current one replaces it, a
// NaN candidate does not.
constexpr float closer(float current, float candidate) { return candidate <= current ? candidate : current; }

// A remote takes part in the checkpoint distance when it is the default partner (always, as shipped) or rides in a vehicle.
constexpr bool countsAtCheckpoint(bool isDefaultPartner, bool riding) { return isDefaultPartner || riding; }

}  // namespace trail_blazing_rules
