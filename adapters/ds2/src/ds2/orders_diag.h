#pragma once
// DS2-internal: log-only instruments for the partner-cargo checks (docs: tools/ds2/out/analysis/TERMINAL_PARTNER_CARGO.md, "Orders
// plan"), on only with adapter.ini orders_diagnostics=1 so they never ship enabled. They log: the callers of the carrier-group
// headline getter 0x1414a13a0 (which UI function builds a group), whether the hand-over gather 0x14119b930 listed the remote's
// owner, and who calls the carried-by-player set 0x1411d7530 and how much the scoped walk of the remote's owner added.
#include <cstdint>

namespace orders_diag {

// Start-up: installs the two hooks (the headline getter and the hand-over gather).
void installEarly();

// Called by partner_cargo's carried-set detour: `caller` is the return address into the game, `added` the number of pieces the
// walk of the remote's owner added to the collector. Logs each distinct caller once, with how many pieces the remote's owner
// holds (to compare with `added`: equal means the walk reaches the child owners too). No-ops when disabled.
void noteCarriedSet(const void* caller, uint32_t added);

}  // namespace orders_diag
