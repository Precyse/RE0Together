#pragma once
// DS2-internal: the remote player the adapter creates for the partner (see remote_body.h), seen by the modules that
// adapt engine behaviour to it. Addresses are 0 until the player exists.
#include <cstdint>

namespace remote_player {

uintptr_t player();      // its PlayerGame
uintptr_t entity();      // its DSPlayerEntity
uintptr_t samEntity();   // the local player's DSPlayerEntity

// The slot of the peer it stands for.
uint8_t slot();

// Whether the remote has its controller and camera mode (the spawn has finished).
bool isLive();

// The local player is about to leave the world (a fast travel, an area change): the body is taken down the way a return to
// the title takes it (unlisted, camera and markers released); it is built again once gameplay settles in the new place.
// Simulation thread. No body is built again for a minute: the load screen can outlast the player's state machine.
void leave(const char* why);

// The same from any thread; the sim tick takes the body down. `why` must outlive the call (a literal).
void requestLeave(const char* why);

}  // namespace remote_player
