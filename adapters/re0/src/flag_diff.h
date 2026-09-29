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

// Applies one change; a change for a word out of range is ignored.
inline void apply(Words& words, const Change& change) {
    if (change.word >= kWords) return;
    words[change.word] = (words[change.word] | change.set) & ~change.clear;
}

}  // namespace flag_diff
