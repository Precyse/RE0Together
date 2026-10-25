#pragma once
#include <cstdint>

// State mode: the blood, sparks and hit sounds of every applied hit on both machines (hit_fx_wire.h). The enemy's
// simulator records the effect and sound calls its damage function makes and sends them as HIT_FX; the other machines
// make the same calls on their copy one render delay late, the instant their copy of the enemy shows the hit.
namespace hit_fx {

// Hooks the effect and sound functions (bytes checked first) and registers HIT_FX. False when either is missing.
bool enable();

// Game thread, around a damage function this machine runs for an enemy it simulates (enemy_damage_hook::runDamage):
// begin starts recording for the enemy, end sends what was recorded.
void beginHit(uintptr_t enemy);
void endHit();

// Game thread, around the unit move of an enemy this machine simulates (enemy_observer): the sounds its own code plays
// meanwhile are collected and sent once a tick as ENEMY_SOUND.
void beginMove(uintptr_t enemy);
void endMove();

}  // namespace hit_fx
