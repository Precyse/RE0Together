#pragma once
// DS2-internal: the partner's body as a second focus for the NPC managers' activity update (see ds2/sneaking_focus.cpp).
#include <cstdint>

namespace sneaking_focus {

// Start-up: patches the distance computation and the wake test of the NPC managers' shared update body 0x141bd3d50.
// `diagnostics` (adapter.ini diagnostics=1) also hooks the update to log its groups (ds2/sneaking_diag.cpp).
void installEarly(bool diagnostics);

// The squared distance from the entity to the nearer of this machine's player and the partner's body (the engine's own
// arithmetic). Simulation thread.
float nearestSquaredDistance(uintptr_t entity);

// Test switch (test command npcpartner.txt "wake", off by default): a sleeping member nearer the partner is not held asleep by
// the component's wake test. Any thread.
void setSkipWakeBlock(bool skip);

}  // namespace sneaking_focus
