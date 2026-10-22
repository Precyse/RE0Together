#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// The code behind the downed, revive and respawn wait rows RW1 to RW4, RA1 to RA3 and RM1 of revive_sites.h (milestone 16 W2,
// docs/SR2_NOTES.md "Downed, revive and notoriety (M16 W2)"). The rules are in revive_rules.h, the reads in revive_state.h.
namespace revive_thunks {

// {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace revive_thunks
