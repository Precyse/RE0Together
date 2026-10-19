#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 16 W1 (docs/SR2_NOTES.md, "Outfits, items and weapon slots (M16 W1)"): the mission natives that dress a player or lock
// weapon slots reach every remote player, or the one remote a sync mask names (tools/sr2/lua/coop_outfit_sync.lua). Sites:
// outfit_sync_sites.h. Only installed with 3 or more players and the partner sites in place.
namespace outfit_sync {

// Writes the three sets (look sync per remote, targeted item wear, targeted weapon slots), each all or nothing, and with `trace` the
// receiver traces one by one. Each result is logged.
void install(bool trace);

// The code behind OF1 to OF4; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace outfit_sync
