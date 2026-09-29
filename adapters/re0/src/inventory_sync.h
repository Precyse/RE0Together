#pragma once
#include <cstdint>

#include "net_client.h"

namespace inventory_sync {

// Wire payload of INVENTORY (0x0106), reliable, to all: u8 character id (0 Billy, 1 Rebecca) followed by that
// character's 0x40-byte inventory block from sItem. Sent by the character's owner.

// Net thread: queues a peer's block for the game thread.
void onFrame(const GameFrame& frame);

// Game frames since a locally owned character's inventory last changed (UINT32_MAX if it never has).
uint32_t framesSinceLocalChange();

// Registers the per-frame sender and applier. Call after character_owner::enable.
void enable(NetClient& net);

}  // namespace inventory_sync
