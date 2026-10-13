#pragma once
#include <cstdint>

// Milestone 4: the partner-singular code of the game made to work with 3 or more players (docs/SR2_NOTES.md,
// "Partner logic"). Sites and thunks: partner_sites.h and partner_thunks_*.cpp.
namespace partner_patches {

// Start-up check of every site, the experimental ones included. Logs each difference. True when all match.
bool sitesMatch();

// Writes the code edits (all or none) and queues the hooks. `experimental` adds the sites that wait for a live probe;
// `trace` makes the getters log changes of the partner. True when the sites were patched.
bool install(bool experimental, bool trace, uint32_t maxPlayers);

}  // namespace partner_patches
