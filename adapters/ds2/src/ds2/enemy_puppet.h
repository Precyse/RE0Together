#pragma once
// DS2-internal: the guest's tamed enemies, driven by the host's reports (see ds2/enemy_puppet.cpp).
#include <array>
#include <cstdint>

namespace enemy_puppet {

// Start-up: the simulation tick that binds, places and removes the puppets.
void installEarly();

// Puts an enemy's AI to sleep and remembers the entity by UUID (called by the spawn hook as the
// entity is built). Any thread.
void adopt(uintptr_t entity, const std::array<uint8_t, 16>& resourceUuid);

// This machine became a guest: the enemies that spawned before that are tamed on the next simulation tick. Any thread.
void adoptExisting();

// This machine became the host (or lost its host): every tamed enemy gets its AI back and is handed to enemy_host, which
// announces it, on the next simulation tick. Any thread.
void releaseToHost();

// Called from the engine's pose evaluation for any animation manager (any thread): when `owner` is a puppet, writes the
// host's animation variables into `manager`. False for every other entity.
bool animate(uintptr_t manager, uintptr_t owner);

}  // namespace enemy_puppet
