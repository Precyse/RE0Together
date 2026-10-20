#pragma once
#include <cstdint>

// Contact between an enemy and a character in partner_mode=state: the player damage and grab entry (vtable slot 34
// 0x512570, thiscall with five stack dwords, ret 0x14, 0 = refused). Enemies call it from their own update, outside any
// move scope, so a puppet is refused here instead of by the pause bit. The refusal is the contact router: when the
// machine that simulates an Observer-class enemy reaches a puppet, the contact becomes a PLAYER_HIT to the victim's
// owner (`contact_net`) or, for a paired attack, an ENEMY_LEASE that moves the enemy's simulation there (`enemy_lease`).
// Enemies the router does not own (Legacy and Native classes run on both machines, other attackers) stay refused.
namespace player_contact {

// Hooks the entry. Needs `puppet` to be enabled for `puppet::active`. False when the hook could not be installed.
bool enable();

void uninstall();

// Game thread: runs the engine's own entry on `victim`, a character this machine owns, as the enemy `enemy` of this
// machine reaching `point` (world position) with row `kind`, entry `level` of the reaction table. True when the engine
// took the contact (it answers 0 while a script pauses the character).
bool applyToLocal(uintptr_t victim, uintptr_t enemy, const float (&point)[3], uint32_t kind, uint32_t level);

}  // namespace player_contact
