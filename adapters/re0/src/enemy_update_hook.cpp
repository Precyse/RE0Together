#include "enemy_update_hook.h"

#include <windows.h>

#include "enemy_state.h"
#include "log.h"
#include "update_thunk.h"

namespace {

void __stdcall onUpdate(void* enemy, uintptr_t original) {
    if (enemy_state::puppetSkipsUpdate(reinterpret_cast<uintptr_t>(enemy))) return;
    update_thunk::callOriginal(original, enemy);
}

}  // namespace

namespace enemy_update_hook {

bool install() {
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, game::kEnemyVtables.size() * update_thunk::kThunkSize,
                                                      MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!memory) return false;
    size_t patched = 0;
    for (const uintptr_t vtable : game::kEnemyVtables) {
        if (update_thunk::patchVtable(vtable, memory + patched * update_thunk::kThunkSize, onUpdate)) ++patched;
    }
    logger::write("enemy_update_hook: patched %zu of %zu vtables", patched, game::kEnemyVtables.size());
    return patched == game::kEnemyVtables.size();
}

}  // namespace enemy_update_hook
