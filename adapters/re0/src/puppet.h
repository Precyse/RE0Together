#pragma once
#include <cstdint>

#include "net_client.h"

// Receiver side of partner_mode=state (docs/RE0_NOTES.md "Pose puppet"): the remote-owned character is a puppet. Its
// brain is switched off with the engine's own script pause, held for the length of its own move; before the move the
// owner's pose, delayed by `pose_timeline::kRenderDelayMs`, is written and the owner's motion requests are replayed.
namespace puppet {

// Hooks the player damage entry (refused for a puppet) and registers the move scope. Needs the cAnimControl::set hook
// (pose_capture::enable). Safe to call after the game tick hook is live.
bool enable();

void uninstall();

// True while `player` is a puppet: state mode, remote-owned and in play here, and the owner's poses are arriving.
// Always false in input mode.
bool active(uintptr_t player);

// Net thread: POSE_STATE and MOTION_EVENT from the driving peer.
void onFrame(const GameFrame& frame);

}  // namespace puppet
