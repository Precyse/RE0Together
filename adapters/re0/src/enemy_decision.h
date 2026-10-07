#pragma once
#include <cstdint>

#include "enemy_follow_rule.h"

// One decision maker per enemy: the room's enemy owner. Its enemies decide as in vanilla and every decision is
// reported with the pose it was made from; on the machine following the owner, the owner's decision is taken at the
// same engine point, through the class's own setAction, and the pose is set to the owner's there when it is more than
// enemy_follow_rule::kRealignDistance off (between two actions). Execution, movement, animation and hit reactions stay
// native on both machines.
//   Base family (game.h kEnemyThinkFunction): the think step decides. On the follower it still runs (flags, timers),
//   the records it would set are dropped and the owner's newest decision is set instead.
//   Boundary classes (game.h kEnemyBoundaryVtables): their action code decides; the executor's commit
//   (kEnemyActionCommitFunction) marks an action boundary. At the follower's own boundary the owner's newest action of
//   the same state replaces its choice, from the action's first step.
//   Every other class runs natively; only its hits, deaths and room start are shared.
namespace enemy_decision {

// Hooks the think step, the base family's setAction and the executor's commit. False when a hook failed.
bool install();

// Game thread: the owner's decision `seq` for the enemy in `slot` of the loaded room.
void offer(uint8_t slot, uint16_t seq, const enemy_follow_rule::Decision& decision);

// Game thread: following starts mid-room; the owner's current record stands in until its first decision.
void seed(uint8_t slot, const enemy_follow_rule::Decision& decision);

// Game thread: this machine stopped following (room change, owner change, peer gone): waiting decisions are dropped.
void reset();

// Game thread: a hit was replayed on `slot`; its reaction replaces a decision the owner made before the hit.
void onHitReplayed(uint8_t slot);

}  // namespace enemy_decision
