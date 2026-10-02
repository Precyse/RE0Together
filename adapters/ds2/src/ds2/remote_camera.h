#pragma once
// DS2-internal: the remote player's own camera. The engine builds a camera mode per player from its
// DSThirdPersonPlayerCameraComponent, but the remote entity has no such component and the engine's helpers bind
// "the local player" to index 0 (Sam). The remote gets a component of its own and a mode built from it, and the
// component's handlers, which would act on Sam, are skipped for the remote.
#include <cstdint>

namespace remote_camera {

void installEarly();

// Marks the calls this thread makes while it spawns the remote as the remote's (its components cannot be told apart
// from Sam's until the entity exists).
class SpawnScope {
public:
    SpawnScope();
    ~SpawnScope();
};

// Adds the camera component and builds the remote's mode. False when Sam's camera is not readable yet.
bool give();

}  // namespace remote_camera
