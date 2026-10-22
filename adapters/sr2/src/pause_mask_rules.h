#pragma once
#include <cstdint>

// The pure rules of the pause flag (milestone 9 N4, milestone 12, unit tested). The game keeps "the partner paused" in one
// byte [0x252932F] and applies the pause (0x8CA190) on every pausing message. The apply is reference counted, so with
// several players a repeated pause leaks a count and the world stays frozen. Here the byte is a bit per session slot (every
// reader only tests it for zero) and the apply runs only when the byte goes between zero and not zero.
namespace pause_mask_rules {

constexpr uint8_t kLastBit = 7;  // slots 0..7; a higher slot shares the last bit

inline uint8_t bitOf(uint8_t slot) { return static_cast<uint8_t>(1u << (slot < kLastBit ? slot : kLastBit)); }

// The byte before and after an event, and whether the game's apply is to run.
struct Change {
    uint8_t before;
    uint8_t after;
    bool apply;
};

inline bool crossesZero(uint8_t before, uint8_t after) { return (before != 0) != (after != 0); }

// N4a: the member in `slot` paused (`paused`) or resumed. A message that arrives before the local member is in game changes
// the byte only; 0x8CB840 applies it when the member gets there.
inline Change afterMessage(uint8_t mask, uint8_t slot, bool paused, bool localInGame) {
    const uint8_t after = paused ? static_cast<uint8_t>(mask | bitOf(slot)) : static_cast<uint8_t>(mask & ~bitOf(slot));
    return {mask, after, localInGame && crossesZero(mask, after)};
}

// N4b: the member in `slot` left. The apply runs when the byte was set and is now clear.
inline Change afterLeave(uint8_t mask, uint8_t slot) {
    const uint8_t after = static_cast<uint8_t>(mask & ~bitOf(slot));
    return {mask, after, crossesZero(mask, after)};
}

}  // namespace pause_mask_rules
