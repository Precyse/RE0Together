#pragma once
// DS2-internal: the host's list of live enemies (see ds2/enemy_host.cpp). Every enemy the engine spawns is added from the
// spawn hook; the simulation tick announces it, samples its pose and reports when it dies or disappears.
#include <array>
#include <cstdint>
#include <vector>

namespace enemy_host {

// Start-up: the simulation tick that announces, samples and reports the enemies.
void installEarly();

// Any thread (spawn workers): `entity` was just built from an entity resource with this UUID.
void add(uintptr_t entity, const std::array<uint8_t, 16>& resourceUuid);

// An enemy changing hands: the entity and the UUID of the resource it was built from.
struct Handover {
    uintptr_t entity;
    std::array<uint8_t, 16> resourceUuid;
};

// This machine became a guest: the tracked enemies that still exist (they spawned before it was told so), no longer
// tracked. Simulation thread.
std::vector<Handover> release();

}  // namespace enemy_host
