#pragma once
#include "net_client.h"

// Story flags (sFlagManager: doors unlocked, items taken, events seen, enemy flags) stay the same on both machines.
// Each machine sends the bits that changed locally; the receiver applies them and treats them as known, so they are
// not sent back.
namespace flag_sync {

// Wire payload of FLAG_DIFF (0x010C), reliable, to all: u16 count, u16 reserved, then count flag_diff::Change.

// Net thread: queues a peer's changes for the game thread.
void onFrame(const GameFrame& frame);

// Registers the per-frame check. Call before game_tick::install.
void enable(NetClient& net);

}  // namespace flag_sync
