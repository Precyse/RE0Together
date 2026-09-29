#include "enemy_registry.h"

#include <algorithm>

#include "game.h"

namespace enemy_registry {

uintptr_t enemyAt(int slot) {
    if (slot < 0 || slot >= game::kEnemyPoolSlots) return 0;
    const uintptr_t sEnemy = game::readPointer(game::kEnemyGlobal);
    if (!sEnemy) return 0;
    return game::readPointer(sEnemy + game::kEnemyPoolOffset + slot * game::kEnemyPoolEntrySize +
                             game::kEnemyPoolObjectOffset);
}

int slotOf(uintptr_t enemy) {
    if (!enemy) return kNoSlot;
    for (int slot = 0; slot < game::kEnemyPoolSlots; ++slot) {
        if (enemyAt(slot) == enemy) return slot;
    }
    return kNoSlot;
}

bool isEnemy(uintptr_t object) {
    const uintptr_t vtable = game::readPointer(object);
    return std::find(game::kEnemyVtables.begin(), game::kEnemyVtables.end(), vtable) != game::kEnemyVtables.end();
}

}  // namespace enemy_registry
