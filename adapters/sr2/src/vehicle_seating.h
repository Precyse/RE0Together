#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 16 wave 2 (docs/SR2_NOTES.md, "Vehicle seating"): with 3 or 4 players every guest gets its own seat. SE1 gives each guest
// a passenger seat of its own in Drug Trafficking, SE2 restores a checkpoint into the car of the remote that MC4 recorded, SE3 and SE4
// stop the cars and release the holds of every remote at the mission exit, ST1 logs the Lua seat rules (trace=1). The Lua half is
// tools/sr2/lua/coop_seats.lua. Sites: vehicle_seating_sites.h. Only installed with 3 or more players and the partner and mission logic
// in place.
namespace vehicle_seating {

// The code behind SE1 to SE4 and ST1; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

// Checks and writes the three sets (SE2 with `experimental`, the probe CP gate) and with `trace` the trace set, each all or nothing,
// and logs one line of what was patched.
void install(bool experimental, bool trace);

}  // namespace vehicle_seating
