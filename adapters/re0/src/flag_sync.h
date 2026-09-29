#pragma once
#include "flag_diff.h"
#include "net_client.h"

// Story flags (sFlagManager: doors unlocked, items taken, events seen, enemy flags) stay the same on both machines.
// Each machine sends the bits that changed locally; the receiver applies them and treats them as known, so they are
// not sent back.
namespace flag_sync {

// Wire payload of FLAG_DIFF (0x010C), reliable, to all: u16 count, u16 reserved, then count flag_diff::Change.

// Net thread: queues a peer's changes for the game thread.
void onFrame(const GameFrame& frame);

// Game thread: the live flag words, false when unreadable.
bool read(flag_diff::Words& out);

// Game thread: overwrites the flags with the host's live words (join snapshot) and treats them as known.
void applySnapshot(const flag_diff::Words& words);

// Registers the per-frame check. Call before game_tick::install.
void enable(NetClient& net);

}  // namespace flag_sync
