#pragma once
#include <cstdint>

#include "character_owner.h"
#include "game.h"
#include "net_client.h"

// Hit replication: guests request their hits from the host, the host applies and announces them.
namespace enemy_net {

// Guest, game thread: sends the hit to the host. False when it cannot be sent (no slot or no link).
bool requestHit(uintptr_t enemy, character_owner::Character attacker, float distance, const game::HitInfo& info);

// Host, game thread: tells the peer about a hit the host applied.
void announceHit(uintptr_t enemy, character_owner::Character attacker, float distance, const game::HitInfo& info);

// Net thread: queues a HIT_REQUEST (host) or HIT_APPLIED (guest) from the peer for the game thread.
void onFrame(const GameFrame& frame);

// Registers the game thread queue drain.
void enable(NetClient& net);

}  // namespace enemy_net
