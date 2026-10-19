#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "sr2_addresses.h"

// The pure rules of one character type per remote (M16 W1, unit tested; docs/SR2_NOTES.md, "One character type per remote"): which
// styletest_pc_mp<n> a remote player's pool index gets, how many extra stream slots that needs, and the sizes and names around it.
namespace char_types_rules {

constexpr uint32_t kNoLocal = 0xFF;            // the pool index of "no local player"
constexpr uint32_t kNotWritten = 0;            // the type number of the local player's own index
constexpr uint32_t kShippedTypeNumber = 2;     // styletest_pc_mp2, what co-op gives every remote
constexpr uint32_t kFirstExtraTypeNumber = 3;  // styletest_pc_mp3 and up live in category 3
constexpr uint32_t kLastTypeNumber = 12;
constexpr uint32_t kPlayerCategory = 2;
constexpr uint32_t kExtraPlayerCategory = 3;
constexpr uint32_t kMaxExtraSlots = 10;        // the slots category 3 is configured with
constexpr uint32_t kCategoryShift = 24;
constexpr uint32_t kSlotPadding = 0x1000;
constexpr uint32_t kSlotAlignment = 0x100;
constexpr uint32_t kNoRecordId = 0;            // an empty registry slot
constexpr int kNoRecord = -1;

// The type number of the remote at pool index `index`: 0 for the local player's index (not written), 2 when nothing above it needs
// its own type, else 2 + the number of remote indices above it. Indices count down from the top as the game allocates them, so the
// first remote allocated keeps the shipped type. A number above `maxPlayers` or above the last type is not used: the shipped type
// stands.
constexpr uint32_t remoteTypeNumber(uint32_t index, uint32_t localIndex, uint32_t maxPlayers) {
    if (index == localIndex) return kNotWritten;
    if (index >= maxPlayers) return kShippedTypeNumber;
    uint32_t above = maxPlayers - 1 - index;
    if (localIndex > index && localIndex < maxPlayers) --above;
    const uint32_t number = kShippedTypeNumber + above;
    return number > maxPlayers || number > kLastTypeNumber ? kShippedTypeNumber : number;
}

// The extra character slots category 3 needs for `maxPlayers`: the local player and the first remote use category 2.
constexpr uint32_t extraCharacterSlots(uint32_t maxPlayers) {
    return maxPlayers <= kShippedTypeNumber ? 0 : std::min(maxPlayers - kShippedTypeNumber, kMaxExtraSlots);
}

// The bytes of one stream slot for a node of `size` bytes (0x9D6636 to 0x9D666D).
constexpr uint32_t slotBytes(uint32_t size) {
    const uint32_t padded = size + kSlotPadding;
    return (padded + kSlotAlignment - 1) / kSlotAlignment * kSlotAlignment;
}

// "styletest_pc_mp<n>" for n in 2..12, else nothing.
inline std::string typeName(uint32_t number) {
    if (number < kShippedTypeNumber || number > kLastTypeNumber) return {};
    return "styletest_pc_mp" + std::to_string(number);
}

constexpr uint32_t categoryOf(uint32_t id) { return id >> kCategoryShift; }

constexpr uintptr_t categoryBase(uint32_t category) { return sr2::kStreamCategories + category * sr2::kStreamCategoryStride; }

// The pool index of a player human, kNoLocal for a pointer outside the 12 pool slots.
constexpr uint32_t poolIndexOf(uintptr_t human) {
    constexpr uintptr_t kPoolEnd = sr2::kPlayerPool + sr2::kPlayerPoolCount * sr2::kPlayerPoolStride;
    if (human < sr2::kPlayerPool || human >= kPoolEnd) return kNoLocal;
    return static_cast<uint32_t>((human - sr2::kPlayerPool) / sr2::kPlayerPoolStride);
}

// The first registry slot whose record id is `id`, or -1. `registryIds` holds [record + 0x60] per slot, kNoRecordId for an empty one.
constexpr int recordFor(uint32_t id, const uint32_t* registryIds, size_t size) {
    if (id == kNoRecordId) return kNoRecord;
    for (size_t slot = 0; slot < size; ++slot) {
        if (registryIds[slot] == id) return static_cast<int>(slot);
    }
    return kNoRecord;
}

// One entry of the char table trace: "index 1 styletest_pc_mp3 type 0x1234 record 0x5678".
inline std::string assignmentText(uint32_t index, uint32_t number, uint32_t type, uint32_t record) {
    char text[96];
    std::snprintf(text, sizeof(text), "index %u %s type 0x%X record 0x%X", index, typeName(number).c_str(), type, record);
    return text;
}

// One entry of the level load trace: "styletest_pc_mp3 id 0x03000012 loaded 1 record 0x5678".
inline std::string streamedText(uint32_t number, uint32_t id, uint32_t loaded, uint32_t record) {
    char text[96];
    std::snprintf(text, sizeof(text), "%s id 0x%08X loaded %u record 0x%X", typeName(number).c_str(), id, loaded, record);
    return text;
}

}  // namespace char_types_rules
