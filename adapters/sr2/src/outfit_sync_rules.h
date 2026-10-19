#pragma once
#include <cstdint>
#include <string>
#include <string_view>

// The pure rules of milestone 16 W1 (no game access, unit tested): a sync mask of the item wear and weapon slot natives
// (SYNC_LOCAL 1, SYNC_REMOTE 2, SYNC_ALL 3) may carry the number of one remote player above those two bits, which tools/sr2/lua/
// coop_outfit_sync.lua writes as `mask = base + 16 * n` and the game's natives ignore (docs/SR2_NOTES.md, "Outfits, items and
// weapon slots (M16 W1)").
namespace outfit_sync_rules {

constexpr uint32_t kLocalBit = 1;
constexpr uint32_t kRemoteBit = 2;
constexpr uint32_t kTargetStep = 16;  // the number field starts here and is this many values wide
constexpr uint32_t kFirstRemoteNumber = 2;
constexpr uint32_t kLastPlayerNumber = 12;
constexpr std::string_view kNamePrefix = "#PLAYER";
constexpr std::string_view kNameSuffix = "#";

// The player number a mask names, 0 when it names none (no remote bit, or a field outside 2..12).
inline uint32_t targetNumber(uint32_t mask) {
    if (!(mask & kRemoteBit)) return 0;
    const uint32_t number = (mask / kTargetStep) % kTargetStep;
    return number >= kFirstRemoteNumber && number <= kLastPlayerNumber ? number : 0;
}

// True when `name` is exactly "#PLAYER<number>#", the text the game keeps for that player.
inline bool namesPlayer(std::string_view name, uint32_t number) {
    return name == std::string(kNamePrefix) + std::to_string(number) + std::string(kNameSuffix);
}

// What a call does: the game's own call (no number), the remote part sent to the named player only, or nothing but the
// local part (the named player is not in the game).
enum class Route { Original, ToTarget, Nothing };

inline Route route(uint32_t mask, bool targetFound) {
    if (targetNumber(mask) == 0) return Route::Original;
    return targetFound ? Route::ToTarget : Route::Nothing;
}

// The mask without its remote part.
inline uint32_t localPart(uint32_t mask) { return mask & kLocalBit; }

}  // namespace outfit_sync_rules
