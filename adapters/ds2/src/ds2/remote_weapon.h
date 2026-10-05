#pragma once
// DS2-internal: the weapon in the partner body's hand (weapon sync, adapter.ini weapon_sync=1). The body gets a weapon
// entity of the partner's drawn weapon, made the way the engine fills Sam's weapon table (0x140eab2f0 on a free entry of
// the body's own weapon table, whose current index is then set to it), so the weapon is owned by and attached to the
// body and the table's own update puts it in the hand. It is swapped or removed when the partner's weapon changes and removed when the body goes. A shot
// the partner makes sets the fire request byte of that weapon's behavior, so the engine plays the shot (flash, sound,
// tracer, projectile) on the body. Nothing here touches Sam's weapons.
#include <cstdint>

namespace remote_weapon {

// Start-up: registers the per-frame work on the simulation thread. `attachMode` is the SetParent mode the weapon is
// attached with; `diagnostics` makes it log how the body's weapon differs from Sam's weapon of the same id once it is made; the engine's own creation uses ds2/remote_weapon.cpp kEngineAttachMode and nothing is redone for it.
void installEarly(uint8_t attachMode, bool diagnostics);

// Called by the shot detours (ds2/local_weapon.cpp) before the engine makes a shot with `behavior`: logs the first time
// the engine runs a shot of the weapon this module made for the body. Any thread.
void noteEngineShot(uintptr_t behavior);

// The attack type of the damage hit the bullets of the weapon the body holds make (the ammo's own, so Rubber and Normal
// differ), 0 when the body holds none or it is unknown. Simulation thread.
uint16_t attackType();

}  // namespace remote_weapon
