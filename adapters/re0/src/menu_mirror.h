#pragma once
#include <cstdint>

#include "net_client.h"

namespace menu_mirror {

// Wire payload of MENU_STATE (0x0105), reliable, to all: whether the sender has a menu, map, message, save screen,
// pause menu or cutscene open and whether it holds the world (live_menu_rule::Screen: 0 none, 1 holding, 2 live), and
// its room phase then (room_phase.h).
struct MenuState {
    uint8_t open;  // live_menu_rule::Screen
    uint8_t phase;
};
static_assert(sizeof(MenuState) == 2);

// Net thread: remembers which peers have a screen open (an entry expires if the peer stops refreshing it).
void onFrame(const GameFrame& frame);

// A peer has a screen open (also after the hold released: the world runs again, the screen is still open).
bool peerMenuOpen();

// Any thread (the rescue): ends every peer's hold on this machine's world; it holds again only if that peer's next
// MENU_STATE says it still stands still. True when a hold was dropped.
bool releasePeerHold();

// Net thread, every tick: sends MENU_STATE when a local screen opens, closes or changes, and every 2 s while it is open.
void onNetTick(NetClient& net);

// Hooks the per-frame unit update: live_menus runs first, then the local world stands still while a peer's screen
// holds it (menu_hold_rule: a reading screen or cutscene until it closes, the pause menu, the save screen and menus
// without live menus for 20 s; a live menu never) and no local screen holds it already,
// and the submenu open: the inventory shows the focused character, so a player whose own character is the partner
// gets the focus moved to it (locally) for the time the menu is open. Call after the game code is decrypted.
bool enable();

void uninstall();

}  // namespace menu_mirror
