#pragma once
#include <cstdint>

namespace partner_think {

// Gives every driven or locked character that still has a cPlayerSubThink a full cPlayerThink; once the peer is gone
// the partner gets its cPlayerSubThink (partner AI) back. Nothing is applied
// while a door runs or for 30 frames after; the door state is re-read every tick, so this can never stay off. Registers a game_tick callback.
// In partner_mode=state a puppet is skipped here: `giveControllerThink` and `restorePartnerAi` are its enter and exit.
void enable();

// Game thread: gives `player` a full cPlayerThink when it still has a cPlayerSubThink and no door runs or ran in the
// last 30 frames. True when it did.
bool giveControllerThink(uintptr_t player);

// Game thread: gives the game's partner its cPlayerSubThink (partner AI) back when it is no longer co-op controlled
// (the peer left), runs a cPlayerThink and no door runs or ran in the last 30 frames. True when it did.
bool restorePartnerAi(uintptr_t partner);

}  // namespace partner_think
