#pragma once
// The guest's own gear as a list of pieces and the file form of it (pure, unit tested): what the guest gained during a
// session, so the next join can give it back. A piece is a cargo kind in the backpack or in one of the worn slots
// (equip_sync::kMirroredSlots).
#include <cstdint>
#include <string>
#include <vector>

namespace gear_snapshot {

constexpr uint8_t kBackpackSlot = 255;  // not a slot kind: the backpack's cargo

struct Item {
    uint8_t slot = kBackpackSlot;
    uint32_t type = 0;       // the cargo kind, the same on every machine
    uint8_t category = 0;    // backpack pieces only
    float durability = 0;    // backpack pieces only; 0 = a fresh piece
};
using Items = std::vector<Item>;

// What `held` has beyond `base`, counting pieces by (slot, kind): the multiset difference. Used both for what the guest
// gained this session (held minus the join baseline) and for what is still missing (wanted minus held).
Items minus(const Items& held, const Items& base);

// "ds2-gear 1" then one "slot type category durability" line per piece.
std::string format(const Items& items);

// False (items untouched) when the text is not a snapshot of this version or a line is malformed.
bool parse(const std::string& text, Items& items);

}  // namespace gear_snapshot
