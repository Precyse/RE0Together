#pragma once
// DS2-internal: an in-process hardware write watch on one memory address (see ds2/health_watch.cpp), used to find the
// code that writes an enemy's health. Armed and disarmed by a test command (ds2/test_commands.cpp, watchhealth.txt).
#include <cstdint>

namespace health_watch {

// Start-up: registers the per-frame work on the simulation thread.
void installEarly();

// Watches the 4 bytes at `address` for writes on every thread of the game, logging each distinct writing instruction
// once with the return addresses found on its stack. Replaces an earlier watch. Simulation thread.
void arm(uintptr_t address);

// Clears the watch (also done after a fixed number of writes or time). Simulation thread.
void disarm();

}  // namespace health_watch
