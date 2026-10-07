#include "enemy_action.h"

#include <windows.h>

#include "game.h"

namespace {

size_t argcOf(uintptr_t function) {
    for (const game::SetActionFunction& known : game::kEnemySetActionFunctions) {
        if (known.function == function) return known.argc;
    }
    return 0;
}

bool requestUnguarded(uintptr_t enemy, const int32_t* word) {
    const uintptr_t vtable = game::readPointer(enemy);
    const uintptr_t setAction = game::readPointer(vtable + game::kEnemySetActionSlot * sizeof(uint32_t));
    const size_t argc = argcOf(setAction);
    void* self = reinterpret_cast<void*>(enemy);
    if (argc == 4) game::callThiscall<void>(setAction, self, word[0], word[1], word[2], word[3]);
    else if (argc == 2) game::callThiscall<void>(setAction, self, word[0], word[1]);
    else if (argc == 1) game::callThiscall<void>(setAction, self, word[0]);
    return argc != 0;
}

}  // namespace

namespace enemy_action {

bool read(uintptr_t enemy, enemy_action_rule::Action& out) {
    return game::readMemory(enemy + game::kEnemyActionOffset, out.word);
}

bool request(uintptr_t enemy, const enemy_action_rule::Action& action) {
    __try {
        return requestUnguarded(enemy, action.word);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace enemy_action
