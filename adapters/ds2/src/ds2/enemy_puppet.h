#pragma once
// DS2-internal: the guest's tamed enemies, driven by the host's reports (see ds2/enemy_puppet.cpp).
#include <cstdint>

namespace enemy_puppet {

// Start-up: the simulation tick that binds, places and removes the puppets.
void installEarly();

// Puts an enemy entity to sleep (the engine stops updating it) and remembers it by UUID (called by the spawn hook as the
// entity is built). Any thread.
void adopt(uintptr_t entity);

// This machine became a guest: the enemies that spawned before that are tamed on the next simulation tick. Any thread.
void adoptExisting();

// Called from the engine's pose evaluation for any animation manager (any thread): when `owner` is a puppet, writes the
// host's animation variables into `manager`. False for every other entity.
bool animate(uintptr_t manager, uintptr_t owner);

}  // namespace enemy_puppet
