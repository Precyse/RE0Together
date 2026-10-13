#pragma once
#include <cstdint>

// The story flag bitset (sFlagManager +0x20, 0x47 words) holds every script flag family. Each family runs
// from its base (the constant the game's setter adds the operand to: SetEnemyFlag 0x312, SetEffectFlag 0x412,
// SetActFlag 0x612, SetDisplayFlag 0x712, SetBGMFlag 0x812) to the next base, as the generic setter
// (0x59bd50) branches; saves set bits past the last named flag of a family.
// docs/re/RE0_FLAGS.md lists every named flag with the rooms that set and read it.
namespace flag_family {

enum class Family { Story, Enemy, Effect, Act, Display, Bgm, Unmapped };

struct Range {
    Family family;
    uint32_t first;
    uint32_t end;  // one past the last bit
};

constexpr uint32_t kBitsetEnd = 0x47 * 32;

constexpr Range kRanges[] = {
    {Family::Story, 0x000, 0x312},
    {Family::Enemy, 0x312, 0x412},
    {Family::Effect, 0x412, 0x612},
    {Family::Act, 0x612, 0x712},
    {Family::Display, 0x712, 0x812},
    {Family::Bgm, 0x812, kBitsetEnd},
};

inline Family classify(uint32_t bit) {
    for (const Range& range : kRanges) {
        if (bit >= range.first && bit < range.end) return range.family;
    }
    return Family::Unmapped;
}

inline const char* name(Family family) {
    switch (family) {
        case Family::Story: return "story";
        case Family::Enemy: return "enemy";
        case Family::Effect: return "effect";
        case Family::Act: return "act";
        case Family::Display: return "display";
        case Family::Bgm: return "bgm";
        case Family::Unmapped: return "unmapped";
    }
    return "unmapped";
}

}  // namespace flag_family
