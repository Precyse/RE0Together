#pragma once
#include <cstdint>
#include <optional>

// Enemy creation, the same on both machines. Every enemy of a room is created from its spawn record by sEnemy's create
// 0x410bb0 (it ends in the class's spawn init, vtable slot 36), and the creation rolls the game's random generator (the
// base family's variant +0x6ab0 = rand % 3 on a first spawn). The create runs with the random state set from the record
// (spawn_seed.h) and the game's own state is put back right after, so both machines create the same enemy and the rest
// of the game's randomness is untouched. Always on in co-op: a room created before the partner joined matches too.
// A created enemy takes its sEnemy pool slot only in its class's start (vtable slot 32: its class table's insert,
// game::kEnemyTableInserts), after the create returned; the hooked insert binds it then (a game tick also looks). Bound, it clears its pool slot's waiting decision and
// follow state (enemy_decision, enemy_state), so a script spawn that reuses a freed slot never inherits the previous
// enemy's, and the slot remembers which spawn record its enemy was created from: the identity both machines share, which
// the enemy observers bind a stream entry to the local enemy by (until 2026-10-22 the binding ran at the create, found
// no slot, and no machine ever streamed an enemy).
namespace enemy_spawn {

// The spawn an enemy was created from.
struct Origin {
    uint16_t record;   // index of the spawn record
    uint16_t spawnId;  // low word of the record's spawn id (+0x10), the word the game's flag tests use
};

// Hooks the create and the seven per-class table inserts (game::kEnemyTableInserts). `trace` (config trace=1) logs
// every insert with its table, index and pool slot. False when a hook failed.
bool install(bool trace);

// Game thread: the spawn the enemy `enemy` standing in pool slot `slot` was created from, or nothing when it was not
// created through the hook (the slot may have been reused by a creation that bypassed it).
std::optional<Origin> originOf(int slot, uintptr_t enemy);

// State mode, game thread: creates the enemy of spawn record `record` (spawn id `spawnId`) that the simulator has in pool
// slot `streamSlot` and this machine lacks, the way the room load does: the seeded create, the room origin added to its
// position, assigned to the loaded room's record. The engine's own checks apply (a record whose kill flag is set creates
// nothing). True when the enemy was created (it takes a pool slot on its start); false when nothing was created: no
// create has been seen in this room yet (the spawn table is learned from the room load's), the record is not that spawn
// here, or an enemy of the record already stands or is about to.
bool createOnDemand(uint8_t streamSlot, uint16_t record, uint16_t spawnId);

}  // namespace enemy_spawn
