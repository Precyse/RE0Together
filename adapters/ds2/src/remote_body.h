#pragma once
#include <cstdint>

#include "game.h"

// A body per remote player: an NPC the game has already loaded is borrowed, moved to the peer's smoothed pose, and
// put back where it was found when the peer stops reporting. While the peer drives a vehicle the body waits where it
// was found (it cannot sit in the vehicle; the vehicle and the marker show the peer) and follows again afterwards. Poses come from the render thread; the engine calls run
// on the simulation thread. A failed borrow or move turns the body off for that peer (the marker stays).
namespace remote_body {

void setEnabled(bool enabled);

// Render thread, once per frame per visible peer: where that peer's body should be and how fast it is moving.
void setTarget(uint8_t slot, const game::Pose& pose, const world_to_screen::Vec3& velocity);

// Simulation thread (main_thread tick): borrows missing bodies, moves live ones, releases those of departed peers.
void tick();

}  // namespace remote_body
