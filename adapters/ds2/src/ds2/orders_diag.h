#pragma once
// DS2-internal: log-only instruments for the partner-cargo checks (docs: tools/ds2/out/analysis/TERMINAL_PARTNER_CARGO.md, "Orders
// plan"), on only with adapter.ini orders_diagnostics=1 so they never ship enabled. They log: the callers of the carrier-group
// headline getter 0x1414a13a0 (which UI function builds a group), whether the hand-over gather 0x14119b930 listed the remote's
// owner, and who calls the carried-by-player set 0x1411d7530 and how much the scoped walk of the remote's owner added.
#include <cstdint>

namespace orders_diag {

// Start-up: installs the two hooks (the headline getter and the owner-active check 0x14119b2e0, which logs its callers for the
// remote's owner and the answer).
void installEarly();

// Called by partner_cargo's hand-over gather detour (which appends the remote's owner to the query's list when it carries order
// pieces and the game left it out): logs whether the owner exists and whether it had to be appended.
void noteHandOverGather(uintptr_t query, uintptr_t remoteOwner, bool appended);

// Called by partner_cargo's slot-add detour for a piece going into the remote's or the local player's owner: where it came
// from (0 = unknown), and whether that origin is the remote's or the local player's. Logs it, so a move the classifier
// ignored (the cargo menu's own path) shows what it saw.
void noteSlotAdd(bool toRemote, uintptr_t origin, bool originIsRemote, bool originIsLocal);

// Called by partner_cargo's carried-set detour: `caller` is the return address into the game, `added` the number of pieces the
// walk of the remote's owner added to the collector. Logs each distinct caller once, with how many pieces the remote's owner
// holds (to compare with `added`: equal means the walk reaches the child owners too). No-ops when disabled.
void noteCarriedSet(const void* caller, uint32_t added);

}  // namespace orders_diag
