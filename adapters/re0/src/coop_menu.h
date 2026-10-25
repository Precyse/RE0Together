#pragma once

// The Co-op page's menu (docs/RE0_NOTES.md "Menus: the native Co-op page"): its row table, its view source and the
// actions behind its rows, registered with native_menu. The rows' rules and text are coop_menu_rule.h.
namespace coop_menu {

// Registers the menu and its actions. Call once at start, before native_page::enable.
void enable();

}  // namespace coop_menu
