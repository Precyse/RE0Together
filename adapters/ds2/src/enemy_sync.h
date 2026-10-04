#pragma once
// The host's enemies, mirrored as puppets on the guests (ENEMY_SPAWN / ENEMY_STATE / ENEMY_GONE, enemy_wire.h). The host
// reports every enemy it spawns; a guest builds a puppet from its own vetoed spawn request and places it where the
// host has the enemy.
#include "net_client.h"

namespace enemy_sync {

// Net thread.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace enemy_sync
