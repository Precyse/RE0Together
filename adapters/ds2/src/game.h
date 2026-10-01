#pragma once
// The engine-specific layer: everything the co-op code needs from the running game. One implementation per game
// build (src/ds2/game.cpp for DEATH STRANDING 2); the rest of the adapter only talks to this interface.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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

// Starts following which use locations are driven by the story's sequence networks (terminals, order and quest
// triggers). Call once at start-up, before the world loads, so none is missed. False on an unsupported build.
bool watchInteractions();

// Guest restriction: while true the local player cannot claim sequence-network use locations, so the host alone runs
// the world's progress; vehicles, cargo and other interactions stay usable. Needs watchInteractions(). Any thread.
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

// One piece of cargo the local player carries.
struct Cargo {
    uint64_t handle = 0;  // this machine's id of the piece (what removeCargo takes)
    uint32_t type = 0;    // the kind of cargo, the same on every machine (what addCargo takes)
    std::string name;     // its display name in the game's language
};

// What the local player's backpack holds: cargo, and weapons and tools stowed in it, but not the equipped gear.
// Any thread.
std::vector<Cargo> carriedCargo();

// A piece lying loose in the world (on no one's rack, in no locker or vehicle).
struct LooseCargo {
    uint64_t handle = 0;
    uint32_t type = 0;
    world_to_screen::Vec3 position;
};

// Loose pieces within `radius` metres of `around`. Any thread.
std::vector<LooseCargo> looseCargo(const world_to_screen::Vec3& around, double radius);

// Asks the game to create a piece of cargo of `type` on the local player's backpack (the game's own request queue,
// served on its next update). False when the request could not be queued. Any thread.
bool addCargo(uint32_t type);

// Asks the game to delete a carried piece (taking it off the player first). Any thread.
bool removeCargo(uint64_t handle);

}  // namespace game
