#pragma once
// The game-thread tick: REFramework's UpdateBehavior and LateUpdateBehavior application entries. Logs the first call of
// each with its thread (probe P1-2: both must run on one thread), beats the hang watchdog, and runs the session at a
// fixed 30 Hz (at most 4 catch-up steps after a long frame): session, offline, then while a matching peer is linked
// the owner stream, the world clock and the peer reports.
namespace tick {

// Registers both entries; false when REFramework refused one.
bool install();

}  // namespace tick
