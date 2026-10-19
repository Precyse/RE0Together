#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 16 wave 2 (docs/SR2_NOTES.md, "Store, clothing and activity level menus (milestone 16 wave 2)"): the store and the
// host's garage hide every remote player human, the garage refuses a vehicle with any remote in its box, and the activity level menu
// opens on the machine of the activity's owner with the guests' answers going to the host. Sites: store_sites.h. Only installed with
// 3 or more players and the milestone 9 logic in place.
namespace store_sync {

// The code behind a store site; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bindingOf(partner_sites::Thunk thunk);

// Writes the three sets (hide, garage, activity level), each all or nothing, and logs each result.
void install(bool trace);

}  // namespace store_sync
