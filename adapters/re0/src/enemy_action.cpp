#include "enemy_action.h"

#include <windows.h>

#include <algorithm>
#include <array>

#include "enemy_state.h"
#include "game.h"
#include "log.h"
#include "set_action_thunk.h"

namespace {

struct Patched {
    uintptr_t vtable;
    uintptr_t original;
    size_t argc;
};

std::array<Patched, game::kEnemyVtables.size()> g_patched{};
size_t g_patchedCount = 0;

const Patched* patchedFor(uintptr_t vtable) {
    for (size_t i = 0; i < g_patchedCount; ++i) {
        if (g_patched[i].vtable == vtable) return &g_patched[i];
    }
    return nullptr;
}

size_t argcOf(uintptr_t function) {
    for (const game::SetActionFunction& known : game::kEnemySetActionFunctions) {
        if (known.function == function) return known.argc;
    }
    return 0;
}

void callOriginal(uintptr_t original, size_t argc, void* enemy, const int32_t* word) {
    if (argc == 4) game::callThiscall<void>(original, enemy, word[0], word[1], word[2], word[3]);
    else if (argc == 2) game::callThiscall<void>(original, enemy, word[0], word[1]);
    else game::callThiscall<void>(original, enemy, word[0]);
}

// The enemy's own AI choosing a behaviour: on a puppet the owner's record decides instead.
void __stdcall onSetAction(void* enemy, uintptr_t original, const int32_t* args) {
    if (enemy_state::puppetOwnsAction(reinterpret_cast<uintptr_t>(enemy))) return;
    callOriginal(original, argcOf(original), enemy, args);
}

bool requestUnguarded(uintptr_t enemy, const enemy_action_rule::Action& action) {
    const Patched* patched = patchedFor(game::readPointer(enemy));
    if (!patched) return false;
    callOriginal(patched->original, patched->argc, reinterpret_cast<void*>(enemy), action.word);
    return true;
}

}  // namespace

namespace enemy_action {

bool read(uintptr_t enemy, enemy_action_rule::Action& out) {
    return game::readMemory(enemy + game::kEnemyActionOffset, out.word);
}

bool install() {
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, game::kEnemyVtables.size() * set_action_thunk::kThunkSize,
                                                      MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!memory) return false;
    size_t skipped = 0;
    for (const uintptr_t vtable : game::kEnemyVtables) {
        const size_t argc = argcOf(game::readPointer(vtable + game::kEnemySetActionSlot * sizeof(uint32_t)));
        const uintptr_t original =
            argc ? set_action_thunk::patchVtable(vtable, memory + g_patchedCount * set_action_thunk::kThunkSize, argc,
                                                 onSetAction)
                 : 0;
        if (original) g_patched[g_patchedCount++] = {vtable, original, argc};
        else ++skipped;
    }
    logger::write("enemy_action: patched %zu of %zu vtables", g_patchedCount, game::kEnemyVtables.size());
    return skipped == 0;
}

bool request(uintptr_t enemy, const enemy_action_rule::Action& action) {
    __try {
        return requestUnguarded(enemy, action);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace enemy_action
