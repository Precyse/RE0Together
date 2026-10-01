#pragma once
// The engine-specific layer: everything the co-op code needs from the running game. One implementation per game
// build (src/ds2/game.cpp for DEATH STRANDING 2); the rest of the adapter only talks to this interface.
#include <optional>

#include "world_to_screen.h"

namespace game {

struct Pose {
    world_to_screen::Vec3 position;  // world metres, Z up
    float yaw = 0;                   // radians about the up axis
};

// Finds the engine objects in the running image. False while the game has not built them yet (retry later) or on
// an unsupported build.
bool resolve();

// The local player character (Sam).
std::optional<Pose> localPlayer();

// The camera the game renders from.
std::optional<world_to_screen::Camera> camera();

}  // namespace game
