#pragma once
#include <cstdint>

// DS2-internal: a player entity's DSPlayerState, the engine's humanoid state object (reached through the entity's
// DSPlayerComponent), and the action plugins it holds.
namespace ds2 {

// The DSPlayerRideVehicleActionPlugin of the player entity, or 0.
uintptr_t ridePlugin(uintptr_t playerEntity);

// Whether the player entity's state machine is running, which it is only once gameplay has started: while the world is
// still loading the entity already exists, but its core action plugin is idle.
bool inGameplay(uintptr_t playerEntity);

}  // namespace ds2
