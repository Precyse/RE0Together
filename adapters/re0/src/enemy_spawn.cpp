#include "enemy_spawn.h"

#include "enemy_decision.h"
#include "enemy_registry.h"
#include "enemy_state.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "scene.h"
#include "spawn_seed.h"

namespace {

using CreateFunction = void*(__fastcall*)(void* self, void* edx, uint32_t a, uint32_t index, uint32_t b);
CreateFunction g_originalCreate = nullptr;

constexpr uint32_t kIndexMask = 0xffff;  // the create reads the record index from the low word of its argument

// The random state this record's creation starts from on every machine, or false when the record is unreadable.
bool seedFor(void* self, uint32_t index, spawn_seed::State& out) {
    const uint32_t record = index & kIndexMask;
    if (record >= game::kSpawnRecordCount) return false;
    const uintptr_t address =
        reinterpret_cast<uintptr_t>(self) + game::kSpawnTableOffset + record * game::kSpawnRecordSize;
    uint32_t kind = 0;
    uint32_t spawnId = 0;
    if (!game::readMemory(address + game::kSpawnRecordKindOffset, kind) ||
        !game::readMemory(address + game::kSpawnRecordIdOffset, spawnId)) {
        return false;
    }
    out = spawn_seed::stateFor(scene::current(), static_cast<uint16_t>(record), kind, spawnId);
    return true;
}

void* createSeeded(void* self, void* edx, uint32_t a, uint32_t index, uint32_t b) {
    spawn_seed::State seed;
    if (!seedFor(self, index, seed)) return g_originalCreate(self, edx, a, index, b);
    const game::RandomState saved = game::readRandomState();
    game::writeRandomState(seed);
    void* const enemy = g_originalCreate(self, edx, a, index, b);
    game::writeRandomState(saved);
    return enemy;
}

// A new enemy in a pool slot (a room load, or a script spawn reusing a freed slot) starts with nothing left over from
// the slot's previous enemy: no waiting decision, and it is aligned with the owner's on the next snapshot.
void forgetPreviousOccupant(void* enemy) {
    const int slot = enemy_registry::slotOf(reinterpret_cast<uintptr_t>(enemy));
    if (slot == enemy_registry::kNoSlot) {
        logger::write("enemy_spawn: created enemy %p is not in the pool yet", enemy);
        return;
    }
    enemy_decision::forgetSlot(static_cast<uint8_t>(slot));
    enemy_state::forgetSlot(static_cast<uint8_t>(slot));
}

void* __fastcall createDetour(void* self, void* edx, uint32_t a, uint32_t index, uint32_t b) {
    void* const enemy = createSeeded(self, edx, a, index, b);
    if (enemy) forgetPreviousOccupant(enemy);
    return enemy;
}

}  // namespace

namespace enemy_spawn {

bool install() {
    return hooks::install("enemy create", game::kEnemyCreateFunction, reinterpret_cast<void*>(createDetour),
                          reinterpret_cast<void**>(&g_originalCreate));
}

}  // namespace enemy_spawn
