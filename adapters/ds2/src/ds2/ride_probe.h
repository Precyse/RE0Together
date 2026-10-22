#pragma once
// DS2-internal: log-only probe (P1) of the game's own F confirm on a vehicle the partner drives, on only with adapter.ini
// diagnostics=1 so it never ships enabled. It logs, for the local player's ride plugin only, each change in what the
// engine's TryStart 0x1410047d0 decides: the early exit, the request path (kind 3 or 9) and the prompt path with its
// first gate 0x141011b20, the reach float, the two blocker flags and the prompt's target. The first line that differs
// from the same press on a free vehicle names where the confirm is dropped (docs/DS2_NOTES.md, "Probe P1").
namespace ride_probe {

void installEarly();

}  // namespace ride_probe
