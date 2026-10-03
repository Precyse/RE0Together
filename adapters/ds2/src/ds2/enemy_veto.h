#pragma once
// DS2-internal: the guest's veto of native enemy spawns (see ds2/enemy_veto.cpp).
namespace enemy_veto {

// Start-up: the detour on EntitySpawnInfo::CreateEntity. It passes everything through until game::vetoEnemies(true).
void installEarly();

}  // namespace enemy_veto
