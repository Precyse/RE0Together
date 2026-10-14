#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// The pure rules of milestone 5i (unit tested): the per pool index look tables that must start empty when a player's
// character instance is created, and the trace line of a look send (docs/SR2_NOTES.md, "Looks").
namespace look_reset_rules {

constexpr uint32_t kPoolIndices = 12;  // the tables have a row per pool index of a player human
constexpr size_t kTintEntries = 8;
constexpr size_t kTintEntryBytes = 16;  // {character instance, slot, tint A, tint B}
constexpr size_t kTintRowBytes = kTintEntries * kTintEntryBytes;

constexpr uintptr_t kAppliedCounts = 0x1FC8C90;   // per pool index: how many tints the row holds
constexpr uintptr_t kAppliedEntries = 0x1FC8CC0;  // per pool index: the row of kTintEntries entries

// One run of bytes to zero.
struct Range {
    uintptr_t address;
    size_t size;

    bool operator==(const Range&) const = default;
};

// The tables the character instance create must clear for `poolIndex` (none for an index outside the tables): the applied
// tints (count and entries, never reset after boot), the pending tints, and the pending body morph.
inline std::vector<Range> rangesFor(uint32_t poolIndex) {
    if (poolIndex >= kPoolIndices) return {};
    constexpr uintptr_t kPendingCounts = 0x1FC8660;
    constexpr uintptr_t kPendingEntries = 0x1FC8690;
    constexpr uintptr_t kMorphCounts = 0x1FC85A0;
    constexpr uintptr_t kMorphValues = 0x1FC85D0;
    constexpr size_t kMorphValueBytes = 12;  // three dwords
    return {{kAppliedCounts + poolIndex * sizeof(uint32_t), sizeof(uint32_t)},
            {kAppliedEntries + poolIndex * kTintRowBytes, kTintRowBytes},
            {kPendingCounts + poolIndex * sizeof(uint32_t), sizeof(uint32_t)},
            {kPendingEntries + poolIndex * kTintRowBytes, kTintRowBytes},
            {kMorphCounts + poolIndex * sizeof(uint32_t), sizeof(uint32_t)},
            {kMorphValues + poolIndex * kMorphValueBytes, kMorphValueBytes}};
}

// A body part tint as a look send packs it.
struct Tint {
    uint32_t slot;
    uint32_t a;
    uint32_t b;
};

// The trace line of a send: `looks: send N body tints from index K: slot s a/b ...`.
inline std::string describeSend(uint32_t poolIndex, const std::vector<Tint>& tints) {
    std::string line = "looks: send " + std::to_string(tints.size()) + " body tints from index " + std::to_string(poolIndex) + ":";
    for (const Tint& tint : tints) {
        char text[64];
        snprintf(text, sizeof(text), " slot %u %08X/%08X", tint.slot, tint.a, tint.b);
        line += text;
    }
    return line;
}

}  // namespace look_reset_rules
