#pragma once

// Enemy creation, the same on both machines. Every enemy of a room is created from its spawn record by sEnemy's create
// 0x410bb0 (it ends in the class's spawn init, vtable slot 36), and the creation rolls the game's random generator (the
// base family's variant +0x6ab0 = rand % 3 on a first spawn). The create runs with the random state set from the record
// (spawn_seed.h) and the game's own state is put back right after, so both machines create the same enemy and the rest
// of the game's randomness is untouched. Always on in co-op: a room created before the partner joined matches too.
namespace enemy_spawn {

// Hooks the create. False when the hook failed.
bool install();

}  // namespace enemy_spawn
