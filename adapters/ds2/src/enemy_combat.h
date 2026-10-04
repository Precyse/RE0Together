#pragma once
// Combat between a guest and the host's enemies (ENEMY_HIT / PLAYER_HIT / ENEMY_DEATH, combat_wire.h). A guest's damage
// to a puppet goes to the host, which applies it to the real enemy after checking it; a host enemy's damage to the
// partner's body goes to the guest, which applies it to its own player; the host says when an enemy died and the guest
// kills the puppet once.
#include "net_client.h"

namespace enemy_combat {

// Net thread.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace enemy_combat
