#pragma once
// The game's title menu (the GUI app.ui030201, rows app.ui030201.ListInfo): two hooks on it, both under one set.
//  - A co-op guest may not start a new world: onSelectList (the one place a row is chosen) is skipped for the New Game rows
//    (`title_menu: New Game refused`). Every other row runs; its load is steered to the host's slot by load_slot.
//  - auto_load=1 (adapter.ini): once the menu has waited idle for title_menu_rule::kSettleMs, the plugin selects the Continue row
//    (ListItemType.Latest) and runs onSelectList, once; the game's own flow then loads, through requestLoad (load_slot.h). A menu
//    without a Continue row logs its rows and does nothing.
// Found in the decompile, see docs/codemap/dd2.md "Title menu".

namespace title_menu {

// Installs the title_menu hook set; false when it stayed off (logged: names.h rows or calls.h title rows moved).
bool install(bool guest, bool autoLoad);

}  // namespace title_menu
