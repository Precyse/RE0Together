#pragma once
// DS2-internal: with a partner beyond the loaded world, the engine's streaming observer moves toward the partner (see
// ds2/observer_midpoint.cpp). A test switch, off by default.
namespace observer_midpoint {

// Start-up: registers the per-frame work on the simulation thread.
void installEarly();

// Switches the observer move on or off (test command observer.txt). Off puts the observer back on this machine's player.
// Any thread.
void setEnabled(bool enabled);

}  // namespace observer_midpoint
