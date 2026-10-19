#pragma once
#include <cstddef>
#include <cstdint>

// The pure rules of milestone 17 wave 3 (docs/SR2_NOTES.md, "FUZZ (milestone 17 wave 3)"; no game access, unit tested): the roll
// that picks the remote a crime NPC attacks, and the three tests the riot and the crime area make.
namespace fuzz_rules {

constexpr size_t kNoIndex = static_cast<size_t>(-1);
constexpr uint32_t kFallbackSeed = 2463534242u;  // replaces a zero state, which xorshift32 would never leave
constexpr unsigned kShiftLeftFirst = 13;
constexpr unsigned kShiftRight = 17;
constexpr unsigned kShiftLeftSecond = 5;

// The next state of a xorshift32 generator; it is also the roll. A zero state is replaced by kFallbackSeed first.
inline uint32_t nextRoll(uint32_t state) {
    if (state == 0) state = kFallbackSeed;
    state ^= state << kShiftLeftFirst;
    state ^= state >> kShiftRight;
    state ^= state << kShiftLeftSecond;
    return state;
}

// Which of `count` remotes a roll picks, kNoIndex without one.
inline size_t pickIndex(uint32_t roll, size_t count) { return count == 0 ? kNoIndex : roll % count; }

// A rioter joins a player when the squared distance is within the squared radius and the heights differ by less than the window.
// A NaN in either comparison fails it, as the game's x87 compares do.
inline bool rioterNear(float squaredDistance, double radiusSquared, float heightGap, float heightWindow) {
    return squaredDistance <= radiusSquared && heightGap < heightWindow;
}

// A player is in the crime area when it holds a district record whose key is the crime's key (a zero key matches a zero key).
inline bool inCrimeDistrict(uintptr_t record, uint32_t recordKey, uint32_t crimeKey) { return record != 0 && recordKey == crimeKey; }

}  // namespace fuzz_rules
