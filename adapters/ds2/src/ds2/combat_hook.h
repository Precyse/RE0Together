#pragma once
// DS2-internal: the hook on the engine's damage function (see ds2/combat_hook.cpp). Every gameplay damage is applied by
// EntityManagerGame::ApplyDamage; with the combat role set (game::setCombatRole) a guest's damage to a puppet and the
// host's damage to the partner's body are sent to the machine that owns the victim instead of being applied.
namespace combat_hook {

// Start-up: the detour on ApplyDamage and the simulation tick that applies the partner's hits and kills, and on the
// host reports which enemies died.
void installEarly();

// Test only: while on, every hit on the local player is dropped (the way a hit on the partner's body is), whoever sent it.
void setGodMode(bool on);

}  // namespace combat_hook
