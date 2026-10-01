#pragma once
#include "net_client.h"
#include "state_sync.h"

namespace state_correction {

// Net thread: remembers the latest PLAYER_STATE of the driving peer.
void onFrame(const GameFrame& frame);

// Registers the HP sync and the post-move position correction: a remote-owned character is pulled toward its owner's
// reported position (extrapolated by velocity); see position_blend.h for the dead zone, blend and snap rules.
void enable();

// Game thread: for a short window, the next PLAYER_STATE reporting our current room snaps the remote-owned
// character even below the normal drift threshold.
void requestForcedCheck();

// The driving peer's latest PLAYER_STATE (its own character, position, scene id); false before the first one.
bool latestState(state_sync::PlayerState& out);

}  // namespace state_correction
