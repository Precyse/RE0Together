#pragma once
// DS2-internal: damage the partner's body deals on this machine is dropped. The body is a picture of the partner; the
// partner's own machine decides what its shots hit, and the real damage arrives through the combat messages. Without
// this a cosmetic shot of the body (weapon sync) would hurt an enemy a second time.
namespace damage_veto {

// Start-up: the detour on the engine's damage queue 0x140129de0 (every gameplay damage passes it, one step before
// EntityManagerGame::ApplyDamage, so it is independent of the combat hook on that function).
void installEarly();

}  // namespace damage_veto
