#pragma once
#include <cstdint>
#include <cstring>

// Pure rules of the equip sync (no game access, unit tested): when a received inventory block needs the character
// re-equipped, and which weapon resource sets the unit factory releases and requests first.
namespace equip_rule {

constexpr size_t kBlockSize = 0x40;
constexpr size_t kEquippedOffset = 0x38;  // u32 in the block: equipped slot index
constexpr uint32_t kNoSlot = 0xffffffff;
constexpr int32_t kNoWeapon = -1;  // weapon type of an empty slot (setEquipped's answer), also "no request"

inline uint32_t equippedSlot(const uint8_t* block) {
    uint32_t slot = 0;
    std::memcpy(&slot, block + kEquippedOffset, sizeof(slot));
    return slot;
}

inline bool needsRefresh(const uint8_t* before, const uint8_t* after) {
    return equippedSlot(before) != equippedSlot(after);
}

// The menu close's resource step for one character (0x5d7fa0, then 0x5203d0): when the weapon type changes, the set of
// the weapon it leaves is released and the set of the weapon it takes is requested. `requested` is a request of ours
// still loading from an earlier change; a newer slot replaces it.
struct LoadPlan {
    bool releaseHeld;
    bool releaseRequested;
    bool requestNext;
    int32_t requested;  // the request outstanding after the plan, kNoWeapon for none
};

inline LoadPlan planLoad(int32_t held, int32_t next, int32_t requested) {
    const bool changes = next != held;
    const bool wanted = changes && next != kNoWeapon;
    const bool alreadyRequested = wanted && requested == next;
    return {changes, requested != kNoWeapon && !alreadyRequested && requested != held, wanted && !alreadyRequested,
            wanted ? next : kNoWeapon};
}

}  // namespace equip_rule
