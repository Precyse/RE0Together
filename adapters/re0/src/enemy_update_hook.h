#pragma once

// The enemies' per-frame update (vtable slot 41, their AI state dispatch). On the machine that does not own the
// room's enemies it is skipped while the owner's snapshot is fresh (enemy_state::puppetSkipsUpdate).
namespace enemy_update_hook {

// Patches the update slot of every enemy vtable. Call once after the game code is decrypted.
bool install();

}  // namespace enemy_update_hook
