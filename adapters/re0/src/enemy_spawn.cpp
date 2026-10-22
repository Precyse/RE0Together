#include "enemy_spawn.h"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

#include "debug_stats.h"
#include "enemy_decision.h"
#include "enemy_registry.h"
#include "enemy_state.h"
#include "game.h"
#include "game_tick.h"
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

// A created enemy is not in sEnemy's pool when the create returns (every create of the 2026-10-22 runs): its class's
// start (vtable slot 32) puts it in its class's table later, through one of game::kEnemyTableInserts, which are hooked.
// Until then it waits here; the insert binds it.
struct Pending {
    uintptr_t enemy;
    std::optional<enemy_spawn::Origin> origin;  // state mode
    uint32_t ticks = 0;
};
constexpr uint32_t kMaxPendingTicks = 600;  // a created enemy that never takes a slot (an object, not an enemy) is dropped
std::vector<Pending> g_pending;             // game thread only
bool g_trace = false;                       // config trace=1: log every table insert and every dropped create

using InsertFunction = uint32_t(__fastcall*)(void* self, void* edx, void* enemy);
std::array<InsertFunction, game::kEnemyTableInserts.size()> g_originalInsert{};

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

// The spawn record a create was made from, or nothing when the record is unreadable.
std::optional<enemy_spawn::Origin> originFor(void* self, uint32_t index) {
    const uint32_t record = index & kIndexMask;
    uint32_t spawnId = 0;
    if (record >= game::kSpawnRecordCount ||
        !game::readMemory(reinterpret_cast<uintptr_t>(self) + game::kSpawnTableOffset + record * game::kSpawnRecordSize +
                              game::kSpawnRecordIdOffset,
                          spawnId)) {
        return std::nullopt;
    }
    return enemy_spawn::Origin{static_cast<uint16_t>(record), static_cast<uint16_t>(spawnId & kSpawnIdMask)};
}

// The enemy took pool slot `slot`. A new enemy in a slot (a room load, or a script spawn reusing a freed slot) starts
// with nothing left over from the slot's previous enemy: no waiting decision, and it is aligned with the owner's on the
// next snapshot. State mode: the slot remembers the spawn record its enemy was created from.
void bind(int slot, const Pending& pending) {
    enemy_decision::forgetSlot(static_cast<uint8_t>(slot));
    enemy_state::forgetSlot(static_cast<uint8_t>(slot));
    std::optional<Created>& created = g_created[static_cast<size_t>(slot)];
    created.reset();
    if (pending.origin) created = Created{pending.enemy, *pending.origin};
}

// Binds a pending enemy that has taken a pool slot; true when it is done waiting (bound, or waited too long).
bool settle(Pending& pending) {
    const int slot = enemy_registry::slotOf(pending.enemy);
    if (slot != enemy_registry::kNoSlot) {
        bind(slot, pending);
        return true;
    }
    if (++pending.ticks < kMaxPendingTicks) return false;
    if (g_trace) logger::write("enemy_spawn: created unit %p never took a pool slot", reinterpret_cast<void*>(pending.enemy));
    return true;
}

void settlePending() { std::erase_if(g_pending, settle); }

// A class's start put `enemy` in table `table` at `index` (the engine's own moment for the pool slot).
void onInsert(size_t table, void* enemy, uint32_t index) {
    const auto address = reinterpret_cast<uintptr_t>(enemy);
    const int slot = index < game::kEnemyTableEntries
                         ? static_cast<int>((game::kEnemyTableInserts[table].table - game::kEnemyPoolOffset) /
                                                game::kEnemyPoolEntrySize + index)
                         : enemy_registry::kNoSlot;
    const auto pending = std::find_if(g_pending.begin(), g_pending.end(),
                                      [address](const Pending& waiting) { return waiting.enemy == address; });
    const bool addressable = slot != enemy_registry::kNoSlot && slot < game::kEnemyPoolSlots;
    if (g_trace) {
        logger::write("enemy_spawn: table 0x%03x insert of %p -> index %u (slot %d%s%s)",
                      static_cast<unsigned>(game::kEnemyTableInserts[table].table), enemy, index, slot,
                      addressable ? "" : ", not a pool slot here", pending != g_pending.end() ? ", created here" : "");
    }
    if (!addressable || pending == g_pending.end()) return;
    bind(slot, *pending);
    g_pending.erase(pending);
}

template <size_t Table>
uint32_t __fastcall insertDetour(void* self, void* edx, void* enemy) {
    const uint32_t index = g_originalInsert[Table](self, edx, enemy);
    onInsert(Table, enemy, index);
    return index;
}

template <size_t... Tables>
bool installInserts(std::index_sequence<Tables...>) {
    return (hooks::install("enemy table insert", game::kEnemyTableInserts[Tables].function,
                           reinterpret_cast<void*>(&insertDetour<Tables>), reinterpret_cast<void**>(&g_originalInsert[Tables])) &&
            ...);
}

// A create returned this enemy (new, or the one already standing for the record): bound now when it has a slot, else
// on a later tick.
void remember(void* enemy, std::optional<enemy_spawn::Origin> origin) {
    const auto address = reinterpret_cast<uintptr_t>(enemy);
    std::erase_if(g_pending, [address](const Pending& pending) { return pending.enemy == address; });
    Pending pending{address, origin};
    if (!settle(pending)) g_pending.push_back(pending);
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
    if (!enemy) return enemy;
    remember(enemy, state_mode::on() ? originFor(self, index) : std::nullopt);
    if (state_mode::on()) g_context = {self, a};
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

// The create for the loaded room and its placement; the enemy, or null when the create refused or the engine faulted (the
// game's random state is then put back: a fault inside the seeded create would leave it at the spawn's seed).
void* createGuarded(const Context& context, uint32_t index) {
    const game::RandomState randomBefore = game::readRandomState();
    __try {
        void* const enemy = createTracked(context.table, nullptr, static_cast<uint32_t>(context.record), index,
                                          game::readPointer(game::kEnemyCreateFlagsGlobal));
        if (enemy) placeInRoom(reinterpret_cast<uintptr_t>(enemy), context.record);
        return enemy;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        game::writeRandomState(randomBefore);
        return nullptr;
    }
}

// True when an enemy of the pool was created from the record, or one is about to take its slot (the create would hand
// that one back, and placing it again would add the room origin twice). A note counts only while its enemy still
// stands in its slot: records are numbered across rooms, and the note of an enemy that left is never cleared.
bool alreadyCreated(uint16_t record) {
    for (size_t slot = 0; slot < g_created.size(); ++slot) {
        const std::optional<Created>& created = g_created[slot];
        if (created && created->origin.record == record && created->enemy == enemy_registry::enemyAt(static_cast<int>(slot))) {
            return true;
        }
    }
    return std::any_of(g_pending.begin(), g_pending.end(),
                       [record](const Pending& pending) { return pending.origin && pending.origin->record == record; });
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

bool install(bool trace) {
    g_trace = trace;
    game_tick::addCallback("enemy_spawn", settlePending);
    const bool create = hooks::install("enemy create", game::kEnemyCreateFunction, reinterpret_cast<void*>(createDetour),
                                       reinterpret_cast<void**>(&g_originalCreate));
    const bool inserts = installInserts(std::make_index_sequence<game::kEnemyTableInserts.size()>{});
    return create && inserts;
}

std::optional<Origin> originOf(int slot, uintptr_t enemy) {
    if (slot < 0 || slot >= game::kEnemyPoolSlots) return std::nullopt;
    const std::optional<Created>& created = g_created[static_cast<size_t>(slot)];
    if (!created || created->enemy != enemy) return std::nullopt;
    return created->origin;
}

bool createOnDemand(uint8_t streamSlot, uint16_t record, uint16_t spawnId) {
    const uintptr_t loaded = scene::loadedRecord();
    if (!loaded || g_context.record != loaded || !g_context.table) {
        logger::writeUnlessRepeated("enemy_spawn: slot %u not created on demand (no create seen in this room yet)", streamSlot);
        return false;
    }
    uint16_t localSpawnId = 0;
    if (record >= game::kSpawnRecordCount || !spawnIdOf(g_context, record, localSpawnId) || localSpawnId != spawnId) {
        logger::writeUnlessRepeated("enemy_spawn: slot %u not created on demand (record %u is not spawn %u here)", streamSlot, record, spawnId);
        return false;
    }
    if (alreadyCreated(record)) return false;
    if (!createGuarded(g_context, record)) {
        logger::writeUnlessRepeated("enemy_spawn: slot %u not created on demand (record %u refused)", streamSlot, record);
        return false;
    }
    debug_stats::count(debug_stats::Counter::EnemyCreatedOnDemand);
    logger::write("enemy_spawn: slot %u created on demand (record %u), it takes a pool slot on its start", streamSlot, record);
    return true;
}

}  // namespace enemy_spawn
