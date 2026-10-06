#pragma once
#include <cstdint>
#include <cstring>

// Pure rule of the equip sync (no game access, unit tested): an inventory block that arrived from the owner needs the
// character's weapon refreshed when its equipped slot differs from the block it replaced.
namespace equip_rule {

constexpr size_t kBlockSize = 0x40;
constexpr size_t kEquippedOffset = 0x38;  // u32 in the block: equipped slot index
constexpr uint32_t kNoSlot = 0xffffffff;

inline uint32_t equippedSlot(const uint8_t* block) {
    uint32_t slot = 0;
    std::memcpy(&slot, block + kEquippedOffset, sizeof(slot));
    return slot;
}

inline bool needsRefresh(const uint8_t* before, const uint8_t* after) {
    return equippedSlot(before) != equippedSlot(after);
}

}  // namespace equip_rule
