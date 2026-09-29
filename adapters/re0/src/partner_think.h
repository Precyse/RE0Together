#pragma once

namespace partner_think {

// Gives every driven or locked character that still has a cPlayerSubThink a full cPlayerThink; once the peer is gone
// the partner gets its cPlayerSubThink (partner AI) back. Nothing is applied
// while a door runs or for 30 frames after; the door state is re-read every tick, so this can never stay off. Registers a game_tick callback.
void enable();

}  // namespace partner_think
