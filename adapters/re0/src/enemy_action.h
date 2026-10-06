#pragma once
#include <cstdint>

#include "enemy_action_rule.h"

// An enemy's behaviour record {state, action id, a, b} (game.h) and the class's own call that sets it.
namespace enemy_action {

// SEH-guarded read; false when unreadable.
bool read(uintptr_t enemy, enemy_action_rule::Action& out);

// Game thread: tells the enemy to take this record through its class's setAction (vtable slot 63), the call its own
// AI makes; its handlers then start the matching motion. A class whose setAction takes another argument count is left
// alone (nothing is called, false). Also false when the call faulted.
bool request(uintptr_t enemy, const enemy_action_rule::Action& action);

}  // namespace enemy_action
