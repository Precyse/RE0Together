#pragma once
// DS2-internal: the hook on the engine's enemy spawns: the host reports them, a guest tames them (see
// ds2/enemy_spawn.cpp).
namespace enemy_spawn {

// Start-up: the detour on EntitySpawnInfo::CreateEntity. It reports enemies to the host module until
// game::tameEnemies(true).
void installEarly();

}  // namespace enemy_spawn
