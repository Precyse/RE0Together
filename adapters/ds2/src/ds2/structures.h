#pragma once
// DS2-internal: the hooks that report the host's placed and removed structures and apply them on a guest (see
// ds2/structures.cpp).
namespace structures {

// Start-up, before the world loads: detours on the structure submit and RequestRemove, and the guest's apply step on
// the simulation tick (sim_tick::installEarly must run after this).
void installEarly();

}  // namespace structures
