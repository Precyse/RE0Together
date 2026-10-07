#pragma once
#include <cstdint>

#include "enemy_follow_rule.h"

// The base family's decisions (game.h kEnemyThinkFunction): one decision maker per enemy. The room's enemy owner runs
// its think step as usual and reports every record it chose; on the machine following the owner the think step still
// runs (its flags and timers), but the records it would set are dropped and the owner's newest decision is set at that
// same point instead, through the class's own setAction. A gap over enemy_follow_rule::kRealignDistance is closed there,
// between two actions. Action execution, movement, animation and hit reactions stay native on both machines.
namespace enemy_think {

// Hooks the think step and the base family's setAction. False when a hook failed.
bool install();

// Game thread: the owner's decision `seq` for the enemy in `slot` of the loaded room.
void offer(uint8_t slot, uint16_t seq, const enemy_follow_rule::Decision& decision);

// Game thread: following starts mid-room; the owner's current record stands in until its first decision.
void seed(uint8_t slot, const enemy_follow_rule::Decision& decision);

// Game thread: this machine stopped following (room change, owner change, peer gone): waiting decisions are dropped.
void reset();

// Game thread: a hit was replayed on `slot`; its reaction replaces a decision the owner made before the hit.
void onHitReplayed(uint8_t slot);

}  // namespace enemy_think
