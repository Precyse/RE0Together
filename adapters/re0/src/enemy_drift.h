#pragma once
#include <cstddef>
#include <cstdint>

#include "enemy_observer_rule.h"

// The enemy drift trace of partner_mode=state (docs/RE0_NOTES.md "Enemy drift trace"). On the machine that shows the
// room's enemies from the stream it compares each enemy with the sample written into it once its own unit move is over
// and puts back HP and the shot-skip bits when the move changed them; on the shooter it checks every shot against the
// enemies it can hit and the answers the simulator gave. Only drift that lasts and shots that went wrong are logged
// (a few lines per window), everything else is counted into one summary line every 30 s.
namespace enemy_drift {

// Registers the per-frame tick (shot windows, summary). Always true.
bool enable();

// Game thread, right after the unit move of the observed enemy in pool slot `index`: compares it with `written`, the
// sample that drive wrote into it before that move, and corrects HP and +0x6a0c.
void afterMove(size_t index, uintptr_t enemy, uint8_t classIndex, const enemy_observer_rule::Sample& written);

// Game thread, from the muzzle flash of the local player's weapon: one trigger pull.
void shotFired();

// Game thread: a hit of the local player went to its simulator (`answeredHere` when this machine is the simulator and
// applied it already), or could not be routed.
void hitRouted(bool answeredHere);
void hitRefused();

// Game thread: a HIT_APPLIED answer for a hit of the local player arrived.
void hitAnswered();

}  // namespace enemy_drift
