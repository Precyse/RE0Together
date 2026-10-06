#include "enemy_action.h"

#include <windows.h>

#include <algorithm>

#include "game.h"

namespace {

// False when the class's setAction is not one with four arguments, nothing is called then.
bool requestUnguarded(uintptr_t enemy, const enemy_action_rule::Action& action) {
    const uintptr_t vtable = game::readPointer(enemy);
    const uintptr_t function = vtable ? game::readPointer(vtable + game::kEnemySetActionSlot * sizeof(uint32_t)) : 0;
    const auto& known = game::kEnemySetActionFunctions;
    if (!function || std::find(known.begin(), known.end(), function) == known.end()) return false;
    game::callThiscall<void>(function, reinterpret_cast<void*>(enemy), action.word[0], action.word[1], action.word[2],
                             action.word[3]);
    return true;
}

}  // namespace

namespace enemy_action {

bool read(uintptr_t enemy, enemy_action_rule::Action& out) {
    return game::readMemory(enemy + game::kEnemyActionOffset, out.word);
}

bool request(uintptr_t enemy, const enemy_action_rule::Action& action) {
    __try {
        return requestUnguarded(enemy, action);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace enemy_action
