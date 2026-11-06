#pragma once
// The game's title menu (the GUI app.ui030201, rows app.ui030201.ListInfo): a co-op guest may not start a new world. onSelectList
// (the one place a row is chosen) is skipped for the New Game rows (`title_menu: New Game refused`); every other row runs and its
// load is steered to the host's slot by load_slot. The guest's own load needs no row: title_load.h calls the game's load directly.
// Found in the decompile, see docs/codemap/dd2.md "Title menu".

namespace title_menu {

// Installs the title_menu hook set; false when it stayed off (logged: names.h rows or calls.h title rows moved).
bool install();

}  // namespace title_menu
