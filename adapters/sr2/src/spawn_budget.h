#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 5i, section 12: the host crashed at 0x910E42 and 0x90D44E reading a null slot of an ambient spawn group. The
// group create 0x910370 links each group into the active list before it fills the group's element slots, and returns 0
// halfway when the static element pool (2400) runs dry, leaving the half-filled group linked. With 3 or more players the
// host streams chunks and crews around every remote, which empties that pool. A create now runs only when both pools can
// give everything it takes; otherwise it returns 0 before linking anything, as the game's own failure does.
//
// Milestone 11 B6, the pool census (trace=1): the same hook counts the creates and the refused ones per caller (return
// address), and once a minute logs the free groups, elements and NPC humans of the static pools, those counts, the remote
// humans and the player areas. NC1 (0x9D0DE0, the NPC human setup, esi = the human a create returned) counts the creates that
// returned nothing per caller. They answer whether a longer session with three players needs bigger pools (W4, W5).
namespace spawn_budget {

// Under trace=1 each refused create is logged (one line per second), the partial groups found after a create, and the census.
void setTrace(bool trace);

// The code behind NC1; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

// Hooks the group create (inside a hooks batch). True when the hook is queued.
bool installHook();

// Takes the hook out again.
void removeHook();

}  // namespace spawn_budget
