#pragma once

// A "Co-op" entry in the Options top menu that opens our status page (coop_page.h) and returns to the list when the
// page closes; docs/RE0_NOTES.md "Menus: text hook and the Co-op entry in Options". The entry is the game's unused item
// id 6, inserted before Back in the list the top menu builds; the Options router would leave a chosen id 6 stuck, so its
// move function is hooked: when Co-op is chosen the router is parked in a state that does nothing while the page is
// open, and set back to "show the top menu again" when the page closes. Nothing else of the Options screen changes.
namespace options_coop {

// Installs the hooks after checking every site's bytes and gives the item its help line (gui_text_rule::kCoopHelpId).
// Call after the game code is decrypted; false when a site does not match this build (the Options screen is then
// untouched).
bool enable();

void uninstall();

}  // namespace options_coop
