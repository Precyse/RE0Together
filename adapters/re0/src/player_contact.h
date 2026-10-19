#pragma once

// Contact between an enemy and a character in partner_mode=state: the player damage and grab entry (vtable slot 34
// 0x512570, thiscall with five stack dwords, ret 0x14, 0 = refused). Enemies call it from their own update, outside any
// move scope, so a puppet is refused here instead of by the pause bit. Later phases turn the refusal into the contact router.
namespace player_contact {

// Hooks the entry. Needs `puppet` to be enabled for `puppet::active`. False when the hook could not be installed.
bool enable();

void uninstall();

}  // namespace player_contact
