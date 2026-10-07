#pragma once
#include <cstdint>

// Which player an enemy chases. The 15 classes sharing the base update choose it in a selector that picks the nearer of
// sPlayer's controlled and partner characters, and each machine's "controlled" is its own character, so two machines
// can pick different players. The owner reports the character it chose; on the other machine the selector's result is
// replaced by that character's local object.
namespace enemy_target {

// Hooks the selector. False when the hook failed.
bool install();

// The character id (character_owner::Character) of the enemy's target, or enemy_protocol::kNoTarget.
uint8_t read(uintptr_t enemy);

}  // namespace enemy_target
