#pragma once
#include <cstdint>
#include <optional>

#include "net_client.h"
#include "stream_clock.h"

// Receiver side of partner_mode=state (docs/RE0_NOTES.md "Pose puppet"): the remote-owned character is a puppet. Its
// brain is switched off with the engine's own script pause, held for the length of its own move; before the move the
// owner's pose, delayed by `pose_timeline::kRenderDelayMs`, is written and the owner's motion requests are replayed
// (body and weapon sub-controls); the steps the paused move skips (hand pose, light probe, spine bend, hand swaps,
// area id) run around it.
namespace puppet {

// Registers POSE_STATE and MOTION_EVENT with the frame router (policy Any), installs the hooks that concern a puppet
// (`player_contact`, `puppet_hooks`) and registers the move scope and the post-move step. Needs the cAnimControl::set
// hook (pose_capture::enable). Safe to call after the game tick hook is live.
bool enable();

void uninstall();

// True while `player` is a puppet: state mode, remote-owned, and a peer is present (it holds still before the first
// pose arrives, while its owner is in another room, where it waits in a dormant room record without being written, and
// while the stream is lost). Always false in input mode.
bool active(uintptr_t player);

// Game thread: true while `player` is the puppet inside its own move (between the move scope's enter and exit).
bool inMoveScope(uintptr_t player);

// Net thread: POSE_STATE and MOTION_EVENT from the driving peer.
void onFrame(const GameFrame& frame);

// The room (scene id) named by the newest pose of the driving peer, scene::kNone before the first one. Any thread.
uint16_t ownerRoom();

// A copy of the clock this machine keeps for peer `slot` (the one the puppet, weapon effects and enemy streams of that
// peer are shown by), or nothing before the peer's first message. Any thread.
std::optional<stream_clock::StreamClock> clockFor(uint8_t slot);

}  // namespace puppet
