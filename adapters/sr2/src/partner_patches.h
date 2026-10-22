#pragma once
#include <cstdint>

// Milestone 4: the partner-singular code of the game made to work with 3 or more players (docs/SR2_NOTES.md,
// "Partner logic"). Sites and thunks: partner_sites.h and partner_thunks_*.cpp.
namespace partner_patches {

// Start-up check of every site. Logs each difference. True when all match.
bool sitesMatch();

// Writes the code edits (all or none) and queues the hooks; `trace` makes the getters log changes of the partner. True when
// the sites were patched.
bool install(bool trace, uint32_t maxPlayers);

}  // namespace partner_patches
