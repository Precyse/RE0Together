#pragma once
#include <cstdint>

// The pure rules of milestone 17 wave 3, walks and the Mayhem counter (no game access, unit tested; docs/SR2_NOTES.md, "Walks and
// the Mayhem counter (milestone 17 wave 3)"): the end of a ragdoll, which counter or score block a Mayhem destruction goes to, and
// the bonus a counter pays.
namespace walk_rules {

// True when the Fraud tick ends a remote's ragdoll: the ragdoll byte (+0x595) is set and bit 0 of the flags byte (+0x84) is.
inline bool ragdollEnds(uint8_t ragdollByte, uint8_t flagsByte) { return ragdollByte != 0 && (flagsByte & 1) != 0; }

// Where a Mayhem destruction is counted. The game keeps two sides: the local player's, and one for everyone else that only
// ever held the default partner. Local and Game are the game's own fields; Remote is the adapter's, for another remote.
enum class Side { Local, Game, Remote };

// The counter of a district entry a destruction raises. The local player wins over every other flag; another remote than the
// default partner has its own counter; the default partner, an NPC and a destruction without a destroyer stay in the game's.
inline Side destructionCounter(bool byLocal, bool byRemote, bool byDefaultPartner) {
    if (byLocal) return Side::Local;
    return byRemote && !byDefaultPartner ? Side::Remote : Side::Game;
}

// The score block a destruction is written to. `gameIndex` is the game's own choice: 0 for the local player, 1 for the default
// partner (after DVM4, for any remote player). Index 0 is the local block whatever the destroyer; index 1 is the game's second
// block, except for a remote that is not the default partner, which has its own.
inline Side scoreBlock(uint32_t gameIndex, bool byRemote, bool byDefaultPartner) {
    if (gameIndex == 0) return Side::Local;
    return byRemote && !byDefaultPartner ? Side::Remote : Side::Game;
}

constexpr uint32_t kBonusPerUnit = 0;  // the count times the entry's per-unit bonus
constexpr uint32_t kBonusScaled = 1;   // the count times the entry's multiplier
constexpr double kMultiplierUnit = 1.0;

// What the game pays for a count of destructions (0x66BB20..0x66BB79, 0x66BC3B..0x66BC5A).
struct Bonus {
    bool pay = false;
    uint32_t kind = kBonusPerUnit;
    int32_t amount = 0;
};

// The 32-bit integer the game's float to int conversion (cvttsd2si) gives: toward zero, the "integer indefinite" for NaN and
// for a value that does not fit.
inline int32_t truncateLikeGame(double value) {
    constexpr double kLowest = -2147483649.0;
    constexpr double kHighest = 2147483648.0;
    if (!(value > kLowest && value < kHighest)) return INT32_MIN;
    return static_cast<int32_t>(value);
}

// A count above zero pays by the multiplier when it is further than `epsilon` from 1.0 (or is not a number), else by the
// per-unit bonus when that is not zero.
inline Bonus bonusFor(int32_t count, float multiplier, float epsilon, int32_t perUnit) {
    if (count <= 0) return {};
    const float delta = static_cast<float>(static_cast<double>(multiplier) - kMultiplierUnit);
    const bool scaled = !(delta < epsilon) || !(-delta < epsilon);
    if (scaled) return {true, kBonusScaled, truncateLikeGame(static_cast<double>(count) * static_cast<double>(multiplier))};
    if (perUnit == 0) return {};
    return {true, kBonusPerUnit, count * perUnit};
}

}  // namespace walk_rules
