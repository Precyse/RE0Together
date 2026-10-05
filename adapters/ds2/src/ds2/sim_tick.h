#pragma once
// DS2-internal: the simulation thread's per-frame hook. The engine's update of live objects (0x140215460) holds that
// list's lock while it runs, so anything that creates or removes entities has to run just before it, on this thread.
// One detour serves every module: they register a function with `add` and it runs ahead of the engine's update.
#include <cstdint>

namespace sim_tick {

using Callback = void (*)();

// Start-up, before the world loads: installs the detour. Safe to call once; modules may `add` before or after.
void installEarly();

// Counts the times the local player's state machine started running (gameplay began or resumed after a load). A module that
// keeps a baseline of engine state compares it to know the baseline is from before a load.
uint32_t gameplayEpoch();

// The one "in gameplay" signal (the local player's state machine runs). `Settled` also needs it to have stayed on for
// `kGameplaySettle` (the engine turns it on while the world still loads); building anything in the world waits for that.
bool gameplayActive();
bool gameplaySettled();

// When a callback runs. `Gameplay` callbacks walk engine lists (camps, structures, missions, the clock) that are freed
// and rebuilt by a load, so they are skipped from the moment the local player's state machine stops until it runs again.
enum class Gate { Always, Gameplay };

// Registers a function to run every frame on the simulation thread (call from start-up code, not from a callback).
// `name` shows in the per-callback cost log.
void add(Callback callback, const char* name, Gate gate = Gate::Always);

}  // namespace sim_tick
