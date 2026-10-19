#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

// The pure rules of the remote player visuals (milestone 14, unit tested; docs/SR2_NOTES.md, "Remote player visuals (M14)"): the
// immediates of the skin composite pool for any number of players, when a hidden human is also killed off, and the trace text.
namespace player_visuals_rules {

constexpr uint32_t kSkinObjectBytes = 0x78;
constexpr uint32_t kShippedSkinObjects = 2;  // one for the local player, one for the partner
constexpr uint32_t kSkinInteriorA = 0x24;    // the immediates that point into the objects
constexpr uint32_t kSkinInteriorB = 0x4C;
constexpr size_t kSkinImmediates = 8;

// The eight immediates the pool is reached through, in the order of the rows PV3 to PV10: the base, the end (base + count objects),
// then base and end moved by 0x24 (rows PV5/PV6 and PV7/PV8 use the same pair) and by 0x4C.
constexpr std::array<uint32_t, kSkinImmediates> skinPoolImmediates(uint32_t base, uint32_t count) {
    const uint32_t end = base + count * kSkinObjectBytes;
    return {base, end, base + kSkinInteriorA, end + kSkinInteriorA, base + kSkinInteriorA, end + kSkinInteriorA, base + kSkinInteriorB, end + kSkinInteriorB};
}

// The game's test `0 < health` (fldz; fcomp; test ah, 1) on the bits of the float, because the x87 stack is the game's at the points
// the thunks run: positive and not zero, and a NaN counts as positive too (an unordered compare sets the same flag).
constexpr bool healthIsPositive(uint32_t bits) {
    constexpr uint32_t kSign = 0x80000000;
    constexpr uint32_t kInfinity = 0x7F800000;
    const uint32_t magnitude = bits & ~kSign;
    if (magnitude > kInfinity) return true;
    return (bits & kSign) == 0 && magnitude != 0;
}

// The cutscene start (0x6C04FD) calls 0x9B04D0 on a hidden human unless its handle resolves to it, its health is above 0 and it is not
// down (the downed timer is negative): the game's own test of the one human it hid.
constexpr bool needsKill(bool handleResolves, uint32_t healthBits, int32_t downedTimer) {
    return !(handleResolves && healthIsPositive(healthBits) && downedTimer < 0);
}

// "0x2CD1E80, 0x2CD3770" for the trace lines.
inline std::string humansText(const uintptr_t* humans, size_t count) {
    std::string text;
    for (size_t i = 0; i < count; ++i) {
        char part[24];
        std::snprintf(part, sizeof(part), "%s0x%X", i == 0 ? "" : ", ", static_cast<unsigned>(humans[i]));
        text += part;
    }
    return text;
}

}  // namespace player_visuals_rules
