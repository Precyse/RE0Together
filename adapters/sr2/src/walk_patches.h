#pragma once

// Milestone 17 wave 3: the inline walks of the first remote and the Mayhem per-player counters for 3 or 4 players (docs/SR2_NOTES.md,
// "Walks and the Mayhem counter (milestone 17 wave 3)"). Sites: walk_sites.h, thunks: walk_thunks.h and mayhem_blocks.h. Needs the
// milestone 7 activity logic.
namespace walk_patches {

// Start-up check of every set of sites. A set whose bytes differ is logged with its first differing address and left out; the others
// stay. True when at least one set matches.
bool sitesMatch();

// Writes each set that matched, all or nothing per set, and queues its hooks; `trace` makes the thunks log their decisions.
void install(bool trace);

}  // namespace walk_patches
