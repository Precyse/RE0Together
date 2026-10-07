#pragma once
#include <cstdint>

#include "character_owner.h"
#include "game.h"
#include "net_client.h"

// Hit replication. The room's enemy owner runs every hit and sends the HP and random state its damage function
// started from; every other machine replays the hit from the same inputs, so the crit roll, damage, reaction and
// death are the same on both screens. A machine that receives a request applies it as the owner would, and one that
// receives an applied hit replays it, so a moment where the two disagree about the owner loses no hit.
namespace enemy_net {

// Game thread: runs a local player's hit as the room's enemy owner and sends HIT_APPLIED.
void applyAsOwner(uintptr_t enemy, character_owner::Character attacker, uintptr_t attackerObject,
                  const game::HitPoint& point, const game::HitInfo& info);

// Game thread, not the owner: sends the hit to the peer. False when it cannot be sent (no slot or no link).
bool requestHit(uintptr_t enemy, character_owner::Character attacker, const game::HitPoint& point,
                const game::HitInfo& info);

// Net thread: queues a HIT_REQUEST or HIT_APPLIED from the peer for the game thread.
void onFrame(const GameFrame& frame);

// Registers the game thread queue drain.
void enable(NetClient& net);

}  // namespace enemy_net
