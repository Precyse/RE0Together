#pragma once
#include <cstdint>

#include "partner_sites.h"
#include "partner_thunks.h"

// The member leave path (milestone 5i, section 8; docs/SR2_NOTES.md, "Member leave"). The game's leave function 0x8072E0 runs
// on every machine; with 2 players only the host survived it, so every machine could take over the leaver's objects and
// rename its ids. With 3 or more the machines would disagree on the ids. Now:
//   LV1  a guest drops the leaver's records locally (the host's messages replace them),
//   LV2  a machine renames only the records it owns (the host also the leaver's that nobody took over), and notes them,
//   LV3  around the whole function, the owner reissues every record it took over or renamed: destroy the old id on the
//        other members (rop_destroy 0x0F built as the game's 0x8053B0 does, from the old id), queue the creation.
// Milestone 9 N2a / N3: the guest's member left callback runs this pass for another guest that leaves, and ends a cutscene
// only for the host leaving.
namespace leave_path {

// The code the two edit sites of player_cap_patches go to.
struct Thunks {
    uintptr_t dropLeaverRecord;  // LV1
    uintptr_t keepRename;        // LV2
};

Thunks thunks();

// Under trace=1 each drop, rename and reissue is logged.
void setTrace(bool trace);

// LV3: hooks the leave function's entry (inside a hooks batch). True when the hook is queued.
bool installHook();

// Takes the hook out again, when the code edits could not be written after all.
void removeHook();

// N2a: the thunk behind the guest's call of the cutscene cleanup 0x6C6B90 in the member left callback.
partner_thunks::Binding bind(partner_sites::Thunk thunk);


}  // namespace leave_path
