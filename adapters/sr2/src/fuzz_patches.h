#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 17 wave 3: FUZZ for 3 or 4 players (docs/SR2_NOTES.md, "FUZZ (milestone 17 wave 3)"). Sites: fuzz_sites.h, thunks:
// fuzz_thunks.h. Needs the milestone 7 activity logic.
namespace fuzz_patches {

// The code behind a site: FUZZ's own thunks, then the diversion thunk of FZ1 and the NPC AI thunk of FZ12.
partner_thunks::Binding bindingOf(partner_sites::Thunk thunk);

// Start-up check of every set of sites. A set whose bytes differ is logged with its first differing address and left out; the
// others stay. True when at least one set matches.
bool sitesMatch();

// Writes each set that matched, all or nothing per set, and queues its hooks. `trace` makes the thunks log their decisions.
void install(bool trace);

}  // namespace fuzz_patches
