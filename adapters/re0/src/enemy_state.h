#pragma once
#include <cstdint>

#include "enemy_protocol.h"
#include "net_client.h"

// Owner to peer enemy snapshot at 20 Hz. Both machines run their enemies' own update; the snapshot carries what the
// follower needs besides the owner's events (enemy_net: hits and decisions): the target the owner's enemy chases, and
// the owner's state once when following starts mid-room (pose, HP and current record). Drift is only logged here.
namespace enemy_state {

// Game thread: this machine shares the loaded room with the peer, the peer runs its enemies, and the peer's snapshots
// are arriving (enemy_follow_rule::thinksForOwner).
bool followsOwner();

// Game thread: this machine runs the loaded room's enemies and the peer is in the room (it follows them).
bool leadsPeer();

// Game thread: the character id the owner's enemy chases (enemy_protocol::kNoTarget when this machine does not follow
// the owner, the enemy is dead there, or the owner named none).
uint8_t ownerTarget(uintptr_t enemy);

// Net thread: stores the latest ENEMY_STATE from the owner for the game thread; a late one is dropped.
void onFrame(const GameFrame& frame);

// Registers the game thread callback: the owner sends, the follower aligns and reads targets.
void enable(NetClient& net);

}  // namespace enemy_state
