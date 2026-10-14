#pragma once
#include <cstdint>

// The pure rules of the pause flag (milestone 9 N4, unit tested). The game keeps "the partner paused" in one byte
// [0x252932F]: the last pausing message wins and any leave clears it, so with several players the pause overlay and the
// state disagree. Here the byte is a bit per session slot, so every reader, which only tests it for zero, is unchanged.
namespace pause_mask_rules {

constexpr uint8_t kLastBit = 7;  // slots 0..7; a higher slot shares the last bit

inline uint8_t bitOf(uint8_t slot) { return static_cast<uint8_t>(1u << (slot < kLastBit ? slot : kLastBit)); }

// The byte after the member in `slot` paused (`paused`) or resumed.
inline uint8_t afterMessage(uint8_t mask, uint8_t slot, bool paused) {
    return paused ? static_cast<uint8_t>(mask | bitOf(slot)) : static_cast<uint8_t>(mask & ~bitOf(slot));
}

// The byte after the member in `slot` left, and whether that ends the pause: the byte was set and is now clear.
struct Left {
    uint8_t mask;
    bool resumes;
};

inline Left afterLeave(uint8_t mask, uint8_t slot) {
    const uint8_t cleared = static_cast<uint8_t>(mask & ~bitOf(slot));
    return {cleared, mask != 0 && cleared == 0};
}

}  // namespace pause_mask_rules
