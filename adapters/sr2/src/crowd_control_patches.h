#pragma once

// Milestone 17 wave 3: Crowd Control for 3 or 4 players (docs/SR2_NOTES.md, "Crowd Control (milestone 17 wave 3)"). Sites:
// crowd_control_sites.h, thunks: crowd_control_thunks.h. Needs the milestone 7 activity logic.
namespace crowd_control_patches {

// Start-up check of every set of sites. A set whose bytes differ is logged with its first differing address and left out; the
// others stay. True when at least one set matches.
bool sitesMatch();

// Writes each set that matched, all or nothing per set, and queues its hooks; `trace` makes the thunks log their decisions.
void install(bool trace);

}  // namespace crowd_control_patches
