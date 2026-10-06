#pragma once
#include <cstdint>

#include "enemy_action_rule.h"

// An enemy's behaviour record {state, action id, a, b} (game.h) and the class's setAction (vtable slot 63) that sets
// it. On a puppet the enemy's own AI calls to setAction are refused (the owner's record is the only one that counts);
// the owner's record is applied through the original function.
namespace enemy_action {

// SEH-guarded read; false when unreadable.
bool read(uintptr_t enemy, enemy_action_rule::Action& out);

// Patches the setAction slot of every enemy vtable whose implementation is known. Call once after the game code is
// decrypted. False when a patch failed.
bool install();

// Game thread: sets the enemy's record through its class's original setAction (it bypasses the refusal), so its
// handlers start the matching motion. Classes with fewer arguments get the first words of the record. False when the
// class is unknown or the call faulted.
bool request(uintptr_t enemy, const enemy_action_rule::Action& action);

}  // namespace enemy_action
