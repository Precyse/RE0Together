#pragma once
#include "net_client.h"

// Host to guest enemy replication at 10 Hz: HP corrections and position snaps.
namespace enemy_state {

// Net thread: stores the latest ENEMY_STATE from the host for the game thread.
void onFrame(const GameFrame& frame);

// Registers the game thread callback: the host sends, a guest applies.
void enable(NetClient& net);

}  // namespace enemy_state
