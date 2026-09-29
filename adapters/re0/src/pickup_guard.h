#pragma once

// Ends a pickup action cleanly when its item is already gone. On the host the guest's replayed input starts a local
// pickup for the remote-owned character; the guest's FLOOR_TAKE then removes the item and the action's next step
// would dereference a null record (access violation at 0x500da3).
namespace pickup_guard {

// Hooks the pickup action step. Call before game_tick::install.
bool enable();

void uninstall();

}  // namespace pickup_guard
