#pragma once
#include "net_client.h"

// Owner to peer enemy replication at 20 Hz. The machine that does not own the room's enemies shows puppets: HP and
// pose come only from the owner's snapshots (pose blended every tick toward the extrapolated target, snapped only
// after a jump; enemy_puppet_rule.h). The enemy's own update keeps running so it keeps animating.
namespace enemy_state {

// Net thread: stores the latest ENEMY_STATE from the host for the game thread.
void onFrame(const GameFrame& frame);

// Registers the game thread callback: the host sends, a guest applies.
void enable(NetClient& net);

}  // namespace enemy_state
