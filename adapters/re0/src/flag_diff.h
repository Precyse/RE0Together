#pragma once
#include <array>
#include <cstdint>
#include <vector>

// Pure bit-level diff of the story flag words (no game access, unit tested).
namespace flag_diff {

constexpr size_t kWords = 0x47;  // sFlagManager +0x20, 0x11c bytes
using Words = std::array<uint32_t, kWords>;

// One changed word: bits that became set and bits that became clear.
struct Change {
    uint16_t word;
    uint16_t reserved;
    uint32_t set;
    uint32_t clear;
};
static_assert(sizeof(Change) == 12);

// The changes that turn `from` into `to`, one per differing word.
inline std::vector<Change> diff(const Words& from, const Words& to) {
    std::vector<Change> changes;
    for (size_t i = 0; i < kWords; ++i) {
        if (from[i] == to[i]) continue;
        changes.push_back({static_cast<uint16_t>(i), 0, to[i] & ~from[i], from[i] & ~to[i]});
    }
    return changes;
}

// Takes one bit out of the changes; a change left with no bit is removed.
inline void dropBit(std::vector<Change>& changes, uint32_t bit) {
    const uint32_t mask = 1u << bit % 32;
    for (Change& change : changes) {
        if (change.word != bit / 32) continue;
        change.set &= ~mask;
        change.clear &= ~mask;
    }
    std::erase_if(changes, [](const Change& change) { return !change.set && !change.clear; });
}

// Applies one change; a change for a word out of range is ignored.
inline void apply(Words& words, const Change& change) {
    if (change.word >= kWords) return;
    words[change.word] = (words[change.word] | change.set) & ~change.clear;
}

}  // namespace flag_diff
