#pragma once
#include "net_client.h"

namespace menu_mirror {

// Wire payload of MENU_STATE (0x0105), reliable, to all: one byte, 1 while the sender's menu is open.

// Net thread: remembers which peers have a menu open (an entry expires if the peer stops refreshing it).
void onFrame(const GameFrame& frame);

// Net thread, every tick: sends MENU_STATE when a local menu, map, message or save screen opens or closes, and every
// 2 s while it is open.
void onNetTick(NetClient& net);

// Hooks the per-frame unit update so the local world stands still while a peer's menu is open and ours is closed,
// and the submenu open: the inventory shows the focused character, so a player whose own character is the partner
// gets the focus moved to it (locally) for the time the menu is open. Call after the game code is decrypted.
bool enable();

void uninstall();

}  // namespace menu_mirror
