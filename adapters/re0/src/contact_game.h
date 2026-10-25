#pragma once
#include <cstdint>

#include "contact_rule.h"

// The engine reads of the contact router and the grab lease (partner_mode=state). Every access is exception-safe; an
// unreadable value answers false.
namespace contact_game {

// The think state entry (kind, level) of the reaction table puts the victim in. False where the engine's own damage
// entry would assert and read null: a kind past the table, an empty row, a level at or past the row's end.
bool entryState(uint32_t kind, uint32_t level, int32_t& state);

// The three floats at `pointAddress` (the hit point the engine passed) relative to the position of `enemy`.
bool relativePoint(uintptr_t enemy, uintptr_t pointAddress, float (&out)[3]);

// The victim's think group and state and its HP.
bool victimView(uintptr_t victim, contact_rule::VictimView& out);

// Game thread: puts the victim back to neutral through the script's Player_Reset (0x516440), the way a stuck hold is
// ended. False when the call faulted.
bool freeVictim(uintptr_t victim);

}  // namespace contact_game
