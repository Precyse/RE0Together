#pragma once
#include "net_client.h"

// Owner to peer enemy replication at 20 Hz. The machine that does not own the room's enemies shows puppets: HP and
// pose come only from the owner's snapshots (pose blended toward the extrapolated target, snapped only after a jump)
// and the local update (AI) is skipped while the owner's snapshot is fresh (enemy_puppet_rule.h).
namespace enemy_state {

// Game thread: the enemy's own update must not run here (the owner decides what it does).
bool puppetSkipsUpdate(uintptr_t enemy);

// Net thread: stores the latest ENEMY_STATE from the host for the game thread.
void onFrame(const GameFrame& frame);

// Registers the game thread callback: the host sends, a guest applies.
void enable(NetClient& net);

}  // namespace enemy_state
