#pragma once
// DS2-internal: log-only instruments for the damage checks, on only with adapter.ini diagnostics=1 so they never ship
// enabled. They log, for every hit on an enemy of the directory, what the NPC damage component decides: its AI state, the
// flags that make it ignore a hit, and the info the hit produced (damage, stagger, type, the applies flag). Static basis:
// the pre-process step 0x141a45200 sets info +0xFA to 0 and returns when the AI individual's state ([[comp+0xA00]+0x1F0]
// +0x2D8) is 3 or 5, and the result step 0x141a45360 does nothing when info +0xFA is 0 or comp +0x187A is set.
#include <cstdint>

namespace damage_diag {

void installEarly();

// Simulation thread: logs the enemy's entity flags as they change over the next frames (does the engine put a woken enemy
// back to sleep before the partner's hit reaches it). No-op unless installed.
void watchSleep(uintptr_t enemy, uint32_t netId);

}  // namespace damage_diag
