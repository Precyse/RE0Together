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

}  // namespace remote_player
