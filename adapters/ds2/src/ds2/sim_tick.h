#pragma once
// DS2-internal: the simulation thread's per-frame hook. The engine's update of live objects (0x140215460) holds that
// list's lock while it runs, so anything that creates or removes entities has to run just before it, on this thread.
// One detour serves every module: they register a function with `add` and it runs ahead of the engine's update.
namespace sim_tick {

using Callback = void (*)();

// Start-up, before the world loads: installs the detour. Safe to call once; modules may `add` before or after.
void installEarly();

// Registers a function to run every frame on the simulation thread (call from start-up code, not from a callback).
void add(Callback callback);

}  // namespace sim_tick
