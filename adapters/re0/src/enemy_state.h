#pragma once
#include <cstdint>

#include "enemy_protocol.h"
#include "net_client.h"

// Owner to peer enemy state at 20 Hz. Both machines run their enemies' own AI; on the machine that does not own the
// room's enemies each snapshot measures how far an enemy is from the owner's and that error is removed over the next
// ticks, the owner's new decisions are cued through setAction, and the owner's target replaces the local choice
// (enemy_follow_rule.h, enemy_action_rule.h). Hits and deaths come from enemy_net; snapshot HP is only a backstop.
namespace enemy_state {

// Game thread: the character id the owner's enemy chases (enemy_protocol::kNoTarget when this machine is not
// following the owner for that enemy, the enemy is dead, or the owner named none).
uint8_t ownerTarget(uintptr_t enemy);

// Game thread, from enemy_net: the owner's hit on `slot` was replayed here, so older snapshot HP must not undo it.
void onHitReplayed(uint8_t slot);

// Net thread: stores the latest ENEMY_STATE from the owner for the game thread.
void onFrame(const GameFrame& frame);

// Registers the game thread callback: the owner sends, the other machine follows.
void enable(NetClient& net);

}  // namespace enemy_state
