#pragma once
#include <cstddef>
#include <cstdint>

#include "partner_rules.h"

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

// A place on the two-column start grid: `right` columns to the right of the local player, `back` rows behind.
struct Cell {
    uint32_t right;
    uint32_t back;
};

constexpr uint32_t kGridColumns = 2;

// The cell of the remote with `rank` (0 is the default partner, whose cell is the game's own: one to the right of the local player).
constexpr Cell startCell(size_t rank) {
    const auto place = static_cast<uint32_t>(rank + 1);
    return {place % kGridColumns, place / kGridColumns};
}

// The spot of a cell: `first` is the local player's spot, `right` and `forward` the rows of the start record; a row behind is
// `spacing` against the forward row.
constexpr partner_rules::Vec3 startPosition(const partner_rules::Vec3& first, const partner_rules::Vec3& right,
                                            const partner_rules::Vec3& forward, float spacing, Cell cell) {
    const float across = spacing * static_cast<float>(cell.right);
    const float behind = spacing * static_cast<float>(cell.back);
    return {first.x + across * right.x - behind * forward.x, first.y + across * right.y - behind * forward.y,
            first.z + across * right.z - behind * forward.z};
}

// The rank of the remote at `index` of the ascending-slot list: 0 for the default partner, the others in order after it. Without a
// default partner (partner_rules::kNone) every remote ranks after the first cell.
constexpr size_t placementRank(size_t index, size_t defaultIndex) {
    if (index == defaultIndex) return 0;
    return index < defaultIndex ? index + 1 : index;
}

}  // namespace trail_blazing_rules
