#pragma once
#include <string_view>

// A native RE0 screen for our menus (docs/RE0_NOTES.md "Menus: the native Co-op page"): the Options screen's own PC
// Setup page (8 text rows with values, arrows, a cursor, the frame, the key guide and the fades) shows a registered
// native_menu instead of its settings. The title opens the Options screen the way its Options row does; the screen's
// router is hooked to open our page instead of the Options list, and to close the whole screen when the page closes,
// which returns the title to its main menu. The page's input step and row refresh are hooked to run the menu. A menu's
// Link row shows another registered menu on the same page, and cancel on a menu with a parent returns to it.
namespace native_page {

// Checks the sites and installs the three hooks. Call after the game code is decrypted and the menus are registered;
// false when a site does not match this build (the page then never opens).
bool enable();

// Game thread, from the title: the Options screen the title is about to open shows menu `menuId`. False when the page
// is unavailable or the menu unknown (the title must not open the Options screen for it).
bool openFromTitle(std::string_view menuId);

// Game thread: the page closes itself on its next input frame (one that is still opening, as soon as it is open), as a
// confirm that closes would, without a sound.
void requestClose();

// Game thread: the page is open or about to open.
bool active();

void uninstall();

}  // namespace native_page
