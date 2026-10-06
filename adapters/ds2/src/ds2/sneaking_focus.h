#pragma once
// DS2-internal: the partner's body as a second focus for the NPC managers' activity update (see ds2/sneaking_focus.cpp).
#include <cstdint>

namespace sneaking_focus {

// Start-up: patches the distance computation of the NPC managers' shared update body 0x141bd3d50.
void installEarly();

// The squared distance from the entity to the nearer of this machine's player and the partner's body (the engine's own
// arithmetic). Simulation thread.
float nearestSquaredDistance(uintptr_t entity);

}  // namespace sneaking_focus
