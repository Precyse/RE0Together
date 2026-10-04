#pragma once
// DS2-internal: the weapon in the partner body's hand (weapon sync, adapter.ini weapon_sync=1). The body gets a weapon
// entity of the partner's drawn weapon, made the way the engine fills Sam's weapon table (0x140eab2f0 on a table entry
// of our own that reports to the body's table component), so the weapon is owned by and attached to the body and
// follows its hand. It is swapped or removed when the partner's weapon changes and removed when the body goes. A shot
// the partner makes sets the fire request byte of that weapon's behavior, so the engine plays the shot (flash, sound,
// tracer, projectile) on the body. Nothing here touches Sam's weapons.
#include <cstdint>

namespace remote_weapon {

// Start-up: registers the per-frame work on the simulation thread. `attachMode` is the SetParent mode the weapon is
// attached with; the engine's own creation uses ds2/remote_weapon.cpp kEngineAttachMode and nothing is redone for it.
void installEarly(uint8_t attachMode);

}  // namespace remote_weapon
