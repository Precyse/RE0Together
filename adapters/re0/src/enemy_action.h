#pragma once
#include <cstdint>

#include "enemy_action_rule.h"

// An enemy's behaviour record {state, action id, a, b} (game.h) and the class's setAction (vtable slot 63) that sets
// it; the class's handlers start the matching motion.
namespace enemy_action {

// SEH-guarded read; false when unreadable.
bool read(uintptr_t enemy, enemy_action_rule::Action& out);

// Game thread: sets the enemy's record through its class's setAction, so its handlers start the matching motion.
// Classes with fewer arguments get the first words of the record. False when the class's setAction is not one of the
// known implementations (a wrong argument count would unbalance the stack) or the call faulted.
bool request(uintptr_t enemy, const enemy_action_rule::Action& action);

}  // namespace enemy_action
