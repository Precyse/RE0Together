#pragma once
#include <cstdint>

#include "partner_sites.h"
#include "partner_thunks.h"
#include "vehicle_trace_rules.h"

// Milestone 12 VT1 (trace=1 only; docs/SR2_NOTES.md, "Remote vehicles"): the create of a remote player's or another
// machine's vehicle (the vehicle class vtable slot 0x873F30) fails when the creation data records or the static vehicle pool
// of 32 are used up, and the game retries it until its window ends. VT1 swaps that vtable slot for a thunk that calls the
// game's function and counts and names every failed attempt per net id, so a live run shows whether the pool was the reason.
namespace vehicle_trace {

// The remote vehicle creates seen so far and why they failed, for the pool census.
vehicle_trace_rules::Totals totals();

// The vehicles still free in the static pool (32, or 64 once vehicle_pool moved it), and the creation data records still free (counted up to 32).
uint32_t freeVehicles();
uint32_t freeCreationData();

// The code behind VT1 and VT2 (the vehicle class's apply, which drops a vehicle whose stream container is not loaded); {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace vehicle_trace
