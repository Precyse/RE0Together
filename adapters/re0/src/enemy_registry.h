#pragma once
#include <cstdint>

// sEnemy's pool: the slot index is the enemy's network id. Every read is SEH-guarded.
namespace enemy_registry {

constexpr int kNoSlot = -1;

// The enemy in a pool slot, or 0.
uintptr_t enemyAt(int slot);

// Pool slot of an enemy object, or kNoSlot.
int slotOf(uintptr_t enemy);

// True when the object's vptr is one of the enemy class vtables.
bool isEnemy(uintptr_t object);

}  // namespace enemy_registry
