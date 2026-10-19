#pragma once

// Ends a pickup action cleanly when its item is already gone. On the host the guest's replayed input starts a local
// pickup for the remote-owned character; the guest's FLOOR_TAKE then removes the item and the action's next step
// would dereference a null record (access violation at 0x500da3).
//
// State mode (phase 12): the take phase also asks the host's item ledger (item_ledger.h). The engine's step is held
// there until the host grants the item; a refusal, or no answer within a couple of seconds, ends the pickup without it.
namespace pickup_guard {

// Hooks the pickup action step. Call before game_tick::install.
bool enable();

void uninstall();

}  // namespace pickup_guard
