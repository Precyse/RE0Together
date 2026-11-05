#pragma once
// The other players on our map (cd design players.md 7). Icon: a hook on the client's minimap icon decision answers
// for a copy's player key with the outlined arrow from the game's own icon table, without calling the original (it
// would hide the copy). Pin: while a peer's copy is not on this screen but both worlds are Ready in the same field, a
// map pin at the peer's reported position (pin_rule.h).
#include <cstdint>
#include <span>

namespace minimap {

// After the catalogue: the two pin Acks. False when a name is missing.
bool resolveOpcodes();

// Inside the hooks batch; false when the icon function's bytes differ (copies then show no minimap icon).
bool install();

// Main tick.
void tick(uint64_t nowMs, std::span<const uint8_t> slotsPresent);

struct Stats {
    uint32_t iconsGiven = 0, pinsCreated = 0;
};
Stats stats();

}  // namespace minimap
