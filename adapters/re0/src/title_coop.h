#pragma once

// A "Co-op" entry on the title screen's main menu that opens our status page (coop_page.h) and gives the menu back
// when the page closes; docs/RE0_NOTES.md "Menus: the Co-op entry on the title screen". The entry is the menu's own
// fifth row, which the PC build hides (the console "Online Store" row): the title's move is hooked to show the row,
// relabel it (title_label.h), and park the title while the page is open instead of letting the row's choice open the
// Steam store overlay.
namespace title_coop {

// Installs the hook after checking every site's bytes. Call after the game code is decrypted; false when a site does
// not match this build (the title is then untouched).
bool enable();

void uninstall();

}  // namespace title_coop
