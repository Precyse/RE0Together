#pragma once
#include <functional>
#include <string_view>

#include "native_menu_rule.h"

// The registry of the data-driven native menus (docs/RE0_NOTES.md "Menus: the native Co-op page"): a menu is a table
// of rows (native_menu_rule::MenuDef) plus a view source that gives each row's text; every row names an action id, and
// the C++ handler registered under that id receives the row's events. Registration happens once at start, before the
// page's hooks are installed; everything else runs on the game thread inside the page's move.
namespace native_menu {

using native_menu_rule::Event;
using native_menu_rule::MenuDef;
using native_menu_rule::PageView;
using native_menu_rule::Reply;

using Handler = Reply (*)(const Event& event);
using ViewSource = PageView (*)();

void registerAction(std::string_view id, Handler handler);
void registerMenu(const MenuDef& menu, ViewSource view);

// The registered menu with this id, or null.
const MenuDef* find(std::string_view id);

// The menu's rows as its view source shows them now (text sanitized for the game font).
PageView view(const MenuDef& menu);

// Gives an event to its action. A refused event (native_menu_rule::allowed), an action nobody registered (logged once)
// or a handler that faulted (disabled for the session) answers Reject.
Reply dispatch(const MenuDef& menu, const PageView& view, const Event& event);

// From a handler: runs `work` once the page has closed and the Options screen is gone (the title is back in charge).
void afterClose(std::function<void()> work);

// The page closed: runs and clears the queued work.
void runAfterClose();

// The page was dropped without closing: the queued work is forgotten.
void dropAfterClose();

}  // namespace native_menu
