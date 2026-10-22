#pragma once
#include <cstdint>

// Which player an enemy chases. Each class picks the nearer of sPlayer's controlled and partner characters in its own
// selector (game::kTargetSelectors, plus uEnemy2b's), and each machine's "controlled" is its own character, so two
// machines can pick different players. The owner reports the character it chose; on the other machine the selector's
// result is replaced by that character's local object. For the large classes the owner also keeps its choice steady
// (target_hysteresis_rule: an attacker is preferred, a switch holds for a while, a new target must be clearly nearer).
namespace enemy_target {

// Hooks the selectors. False when a hook failed.
bool install();

// The character id (character_owner::Character) of the enemy's target, or enemy_protocol::kNoTarget.
uint8_t read(uintptr_t enemy);

// Game thread, the room's enemy owner: a hit by `characterId` landed on `enemy`.
void noteAttacker(uintptr_t enemy, uint8_t characterId);

}  // namespace enemy_target
