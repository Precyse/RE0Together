#pragma once

// Receiver side of the weapon effects of partner_mode=state: WEAPON_FX from the driving peer waits on a timed queue
// against that peer's stream clock (the one the puppet is shown by), and the due entries are replayed on the puppet
// right after its move, in the owner's order, through the same engine primitives. Nothing here calls the shot, the
// ammo, the shot counter or the rumble, and the capture ignores calls on a puppet, so nothing echoes.
namespace weapon_fx_replay {

// Registers WEAPON_FX with the frame router (policy Any) and the post-move replay. Call before game_tick::install.
bool enable();

}  // namespace weapon_fx_replay
