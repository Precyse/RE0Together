#pragma once
// DS2-internal: waking an entity the engine's streaming put to sleep. The engine sleeps enemies by the distance to the HOST's
// own player, and a sleeping entity is sent no messages (a hit on it never reaches its damage component, it does not fight);
// the partner's body is not a player the streaming counts, so the code that serves the partner wakes what it needs.
#include <cstdint>

namespace ds2 {

// Whether the entity's flags (+0x98) have the asleep bit (9) set; false for memory that cannot be read (the group tables and
// the enemy lists still hold freed entities while a load tears the world down).
bool entityAsleep(uintptr_t entity);

// Asks the engine to wake it (Entity wake 0x1401312b0(entity, 0, 0): sets a wake request, flags bit 36, that the engine serves
// on a later update). False when the call faulted. Simulation thread.
bool wakeEntity(uintptr_t entity);

}  // namespace ds2
