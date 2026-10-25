#pragma once

// A "Co-op" entry on the title screen's main menu that opens the native Co-op page (native_page.h, coop_menu.h); docs/
// RE0_NOTES.md "Menus: the Co-op entry on the title screen". The entry is the menu's own fifth row, which the PC build
// hides (the console "Online Store" row): the title's move is hooked to show the row, relabel it (title_label.h), and
// send its choice down the Options row's path instead of the Steam store overlay. The same hook drives the session load
// from the title (title_load.h) and tells the lobby where this game is.
namespace title_coop {

// Installs the hook after checking every site's bytes. Call after the game code is decrypted; false when a site does
// not match this build (the title is then untouched).
bool enable();

void uninstall();

}  // namespace title_coop
