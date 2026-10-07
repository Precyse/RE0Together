#pragma once
#include <cstdint>

#include "character_owner.h"
#include "enemy_follow_rule.h"
#include "game.h"
#include "net_client.h"

// The owner's enemy events, in the order they happened. Hits: the room's enemy owner runs every hit and sends the HP
// and random state its damage function started from and the HP it ended with; every other machine replays the hit from
// the same inputs, so the crit roll, damage, reaction and death are the same on both screens. A machine that receives a
// request applies it as the owner would, and one that receives an applied hit replays it, so a moment where the two
// disagree about the owner loses no hit. Decisions: the base family's think step choices (enemy_think). Both kinds
// share one queue, so a decision the owner made before a hit is never applied after that hit's reaction.
namespace enemy_net {

// Game thread: runs a hit as the room's enemy owner and sends HIT_APPLIED. `attackerId` is the local player's
// character id, or enemy_protocol::kNoAttacker for damage no player dealt (only its HP outcome can be replayed).
void applyAsOwner(uintptr_t enemy, uint8_t attackerId, uintptr_t attackerObject, const game::HitPoint& point,
                  const game::HitInfo& info);

// Game thread, not the owner: sends the hit to the peer. False when it cannot be sent (no slot or no link).
bool requestHit(uintptr_t enemy, character_owner::Character attacker, const game::HitPoint& point,
                const game::HitInfo& info);

// Game thread, the owner: sends a think step's decision for the enemy in `slot`.
void sendDecision(uint8_t slot, const enemy_follow_rule::Decision& decision);

// Net thread: queues a HIT_REQUEST, HIT_APPLIED or ENEMY_DECISION from the peer for the game thread.
void onFrame(const GameFrame& frame);

// Registers the game thread queue drain.
void enable(NetClient& net);

}  // namespace enemy_net
