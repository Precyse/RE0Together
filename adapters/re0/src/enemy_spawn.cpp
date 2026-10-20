#include "enemy_spawn.h"

#include <array>

#include "debug_stats.h"
#include "enemy_decision.h"
#include "enemy_registry.h"
#include "enemy_state.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "scene.h"
#include "spawn_seed.h"
#include "state_mode.h"

namespace {

using CreateFunction = void*(__fastcall*)(void* self, void* edx, uint32_t a, uint32_t index, uint32_t b);
CreateFunction g_originalCreate = nullptr;

constexpr uint32_t kIndexMask = 0xffff;  // the create reads the record index from the low word of its argument
constexpr uint32_t kSpawnIdMask = 0xffff;

struct Created {
    uintptr_t enemy;
    enemy_spawn::Origin origin;
};
std::array<std::optional<Created>, game::kEnemyPoolSlots> g_created{};  // state mode, game thread only

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

// State mode: remembers the spawn record the enemy in its pool slot was created from.
void noteOrigin(void* self, uint32_t index, void* enemy) {
    const int slot = enemy_registry::slotOf(reinterpret_cast<uintptr_t>(enemy));
    if (slot == enemy_registry::kNoSlot) return;
    std::optional<Created>& created = g_created[static_cast<size_t>(slot)];
    created.reset();
    const uint32_t record = index & kIndexMask;
    uint32_t spawnId = 0;
    if (record >= game::kSpawnRecordCount ||
        !game::readMemory(reinterpret_cast<uintptr_t>(self) + game::kSpawnTableOffset + record * game::kSpawnRecordSize +
                              game::kSpawnRecordIdOffset,
                          spawnId)) {
        return;
    }
    created = Created{reinterpret_cast<uintptr_t>(enemy),
                      {static_cast<uint16_t>(record), static_cast<uint16_t>(spawnId & kSpawnIdMask)}};
}

// state mode, game thread: the spawn table (the create's ECX) and room record of the newest create. A create on demand
// reuses them while that room record is still the loaded one.
struct Context {
    void* table = nullptr;
    uintptr_t record = 0;
};
Context g_context;

// A create plus the bookkeeping every created enemy gets.
void* createTracked(void* self, void* edx, uint32_t a, uint32_t index, uint32_t b) {
    void* const enemy = createSeeded(self, edx, a, index, b);
    if (enemy) forgetPreviousOccupant(enemy);
    if (enemy && state_mode::on()) {
        noteOrigin(self, index, enemy);
        g_context = {self, a};
    }
    return enemy;
}

void* __fastcall createDetour(void* self, void* edx, uint32_t a, uint32_t index, uint32_t b) {
    return createTracked(self, edx, a, index, b);
}

// What the room load does with each enemy it created (0x61b240): the room's origin is added to the position and the
// enemy is assigned to the room record.
void placeInRoom(uintptr_t enemy, uintptr_t record) {
    float origin[3];
    float position[3];
    if (!game::readMemory(record + game::kSceneRecordOriginOffset, origin) ||
        !game::readMemory(enemy + game::kUnitPositionOffset, position)) {
        return;
    }
    for (int axis = 0; axis < 3; ++axis) position[axis] += origin[axis];
    game::writeMemory(enemy + game::kUnitPositionOffset, position);
    game::writeMemory(enemy + game::kUnitPositionWOffset, uint32_t{0});
    void* const info = reinterpret_cast<void*>(game::readPointer(game::kSceneInfoGlobal));
    if (info) game::callThiscall<void>(game::kSceneAssignFunction, info, reinterpret_cast<void*>(record), reinterpret_cast<void*>(enemy));
}

// The create for the loaded room and its placement; the enemy, or null when the create refused or the engine faulted.
void* createGuarded(const Context& context, uint32_t index) {
    __try {
        void* const enemy = createTracked(context.table, nullptr, static_cast<uint32_t>(context.record), index,
                                          game::readPointer(game::kEnemyCreateFlagsGlobal));
        if (enemy) placeInRoom(reinterpret_cast<uintptr_t>(enemy), context.record);
        return enemy;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// True when an enemy of the pool was created from the record (the create would hand that one back, and placing it
// again would add the room origin twice). A note counts only while its enemy still stands in its slot: records are
// numbered across rooms, and the note of an enemy that left is never cleared.
bool alreadyCreated(uint16_t record) {
    for (size_t slot = 0; slot < g_created.size(); ++slot) {
        const std::optional<Created>& created = g_created[slot];
        if (created && created->origin.record == record && created->enemy == enemy_registry::enemyAt(static_cast<int>(slot))) {
            return true;
        }
    }
    return false;
}

// The spawn id the record carries (low word), or false when the record is unreadable.
bool spawnIdOf(const Context& context, uint16_t record, uint16_t& out) {
    uint32_t id = 0;
    const uintptr_t address = reinterpret_cast<uintptr_t>(context.table) + game::kSpawnTableOffset +
                              record * game::kSpawnRecordSize + game::kSpawnRecordIdOffset;
    if (!game::readMemory(address, id)) return false;
    out = static_cast<uint16_t>(id & kSpawnIdMask);
    return true;
}

}  // namespace

namespace enemy_spawn {

bool install() {
    return hooks::install("enemy create", game::kEnemyCreateFunction, reinterpret_cast<void*>(createDetour),
                          reinterpret_cast<void**>(&g_originalCreate));
}

std::optional<Origin> originOf(int slot, uintptr_t enemy) {
    if (slot < 0 || slot >= game::kEnemyPoolSlots) return std::nullopt;
    const std::optional<Created>& created = g_created[static_cast<size_t>(slot)];
    if (!created || created->enemy != enemy) return std::nullopt;
    return created->origin;
}

int createOnDemand(uint8_t streamSlot, uint16_t record, uint16_t spawnId) {
    const uintptr_t loaded = scene::loadedRecord();
    if (!loaded || g_context.record != loaded || !g_context.table) {
        logger::writeUnlessRepeated("enemy_spawn: slot %u not created on demand (no create seen in this room yet)", streamSlot);
        return enemy_registry::kNoSlot;
    }
    uint16_t localSpawnId = 0;
    if (record >= game::kSpawnRecordCount || !spawnIdOf(g_context, record, localSpawnId) || localSpawnId != spawnId) {
        logger::writeUnlessRepeated("enemy_spawn: slot %u not created on demand (record %u is not spawn %u here)", streamSlot, record, spawnId);
        return enemy_registry::kNoSlot;
    }
    if (alreadyCreated(record)) return enemy_registry::kNoSlot;
    void* const enemy = createGuarded(g_context, record);
    const int slot = enemy_registry::slotOf(reinterpret_cast<uintptr_t>(enemy));
    if (!enemy || slot == enemy_registry::kNoSlot) {
        logger::writeUnlessRepeated("enemy_spawn: slot %u not created on demand (record %u refused)", streamSlot, record);
        return enemy_registry::kNoSlot;
    }
    debug_stats::count(debug_stats::Counter::EnemyCreatedOnDemand);
    if (slot == streamSlot) {
        logger::write("enemy_spawn: slot %u created on demand (record %u)", streamSlot, record);
    } else {
        logger::write("enemy_spawn: slot %u created on demand in pool slot %d (record %u)", streamSlot, slot, record);
    }
    return slot;
}

}  // namespace enemy_spawn
