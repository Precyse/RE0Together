#pragma once
// DS2-internal: the local player's weapon, for weapon sync (adapter.ini weapon_sync=1). What Sam has drawn is read from
// the weapon table of his entity (game::localWeaponState), and every shot or throw he makes is caught at the
// CreateAttackRequest of the weapon behavior that makes it (game::takeLocalFires). Enemies' weapons run the same
// functions and are ignored: only a weapon whose owner is the local player counts.
namespace local_weapon {

// Start-up: the detours on the shot functions (ds2/weapon_layout.h kShotFunctions).
void installEarly();

}  // namespace local_weapon
