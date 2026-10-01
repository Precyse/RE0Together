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

// A function the game's simulation thread calls every frame, taking one pointer and returning one
// (DS2: Player::GetLastActivatedCamera); 0 before resolve() succeeds.
uintptr_t frameFunction();

// Guest restriction: while true the local player cannot claim use locations (terminals, orders, quest triggers; for
// now every "F" interaction), so the host alone runs the world's progress. Any thread.
void blockScriptedInteractions(bool block);

// Opaque handle of an entity the adapter created; 0 = none.
using Body = uintptr_t;

// Borrows a humanoid NPC the game has already loaded (the nearest to the player) to serve as a remote player's body.
// Nothing while the background search runs (call again later), then the body, or 0 when none is loaded.
std::optional<Body> borrowBody();

// Where a body stands now (to put a borrowed NPC back when it is released).
std::optional<Pose> bodyPose(Body body);

// Moves a body (the engine's SetWorldTransform) and gives its mover the velocity it is moving with, so its own
// animation can follow. False when it faults; the caller then forgets the body.
bool placeBody(Body body, const Pose& pose, const world_to_screen::Vec3& velocity);

}  // namespace game
