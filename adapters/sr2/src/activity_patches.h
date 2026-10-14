#pragma once

// Milestone 7: activities, diversions and strongholds for 3 or more players (docs/SR2_NOTES.md, "Activities"). Sites:
// activity_sites.h, thunks: activity_thunks.h. Needs the milestone 4 getters.
namespace activity_patches {

// Start-up check of every site of every phase, the experimental ones included. Logs each difference. True when all match.
bool sitesMatch();

// Writes each phase (all or none per phase) and queues its hooks. `experimental` adds the sites that wait for a live
// probe; `trace` makes the thunks log their decisions. True when every phase was patched.
bool install(bool experimental, bool trace);

}  // namespace activity_patches
