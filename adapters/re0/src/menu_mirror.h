#pragma once
#include <cstdint>

#include "net_client.h"

namespace menu_mirror {

// Wire payload of MENU_STATE (0x0105), reliable, to all: whether the sender has a menu, map, message, save screen or
// cutscene open, and its room phase then (room_phase.h).
struct MenuState {
    uint8_t open;
    uint8_t phase;
};
static_assert(sizeof(MenuState) == 2);

// Net thread: remembers which peers have a screen open (an entry expires if the peer stops refreshing it).
void onFrame(const GameFrame& frame);

// A peer has a screen open (also after the hold released: the world runs again, the screen is still open).
bool peerMenuOpen();

// Net thread, every tick: sends MENU_STATE when a local menu, map, message or save screen opens or closes, and every
// 2 s while it is open.
void onNetTick(NetClient& net);

// Hooks the per-frame unit update so the local world stands still while a peer's screen holds it (menu_hold_rule: a
// reading screen or cutscene until it closes, a menu for 20 s) and ours is closed,
// and the submenu open: the inventory shows the focused character, so a player whose own character is the partner
// gets the focus moved to it (locally) for the time the menu is open. Call after the game code is decrypted.
bool enable();

void uninstall();

}  // namespace menu_mirror
