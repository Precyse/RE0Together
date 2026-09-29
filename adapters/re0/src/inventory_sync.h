#pragma once
#include <cstdint>

#include "net_client.h"

namespace inventory_sync {

// Wire payload of INVENTORY (0x0106), reliable, to all: u8 character id (0 Billy, 1 Rebecca), u8 origin, then that
// character's 0x40-byte inventory block from sItem. The owner sends its character's block (origin 0). A menu can
// also change the other player's character (item exchange); that change is sent once when the menu closes
// (origin 1) and the owner applies it to its own character.

// Net thread: queues a peer's block for the game thread.
void onFrame(const GameFrame& frame);

// Game thread: overwrites a character's inventory with the host's live block (join snapshot); for this machine's own
// character the block also becomes the last sent state.
void applySnapshot(uint8_t characterId, const uint8_t (&block)[0x40]);

// Game thread: the live block of a character, false when unreadable.
bool readBlock(uint8_t characterId, uint8_t (&block)[0x40]);

// Game thread, as a submenu opens: remembers the blocks so an exchange made in the menu is found when it closes.
void onMenuOpen();

// Game frames since a locally owned character's inventory last changed (UINT32_MAX if it never has).
uint32_t framesSinceLocalChange();

// Registers the per-frame sender and applier. Call after character_owner::enable.
void enable(NetClient& net);

}  // namespace inventory_sync
