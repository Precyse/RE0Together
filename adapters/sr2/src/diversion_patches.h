#pragma once

// Milestone 16 wave 2: diversions and activities for 3 or 4 players (docs/SR2_NOTES.md, "Diversions and activities (milestone 16
// wave 2)"). Sites: diversion_sites.h, thunks: diversion_thunks.h. Needs the milestone 7 activity logic.
namespace diversion_patches {

// Start-up check of every set of sites, phase B included. A set whose bytes differ is logged with its first differing address
// and left out; the others stay. True when at least one set matches.
bool sitesMatch();

// Writes each set that matched, all or nothing per set, and queues its hooks. Phase B is written with `experimental` only;
// `trace` makes the thunks log their decisions.
void install(bool experimental, bool trace);

}  // namespace diversion_patches
