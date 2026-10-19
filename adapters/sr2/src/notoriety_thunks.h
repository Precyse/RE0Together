#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// The code behind the notoriety rows NW1, NV1, NL1 and NL2 of revive_sites.h (milestone 16 W2, docs/SR2_NOTES.md "Downed, revive and
// notoriety (M16 W2)"): a crime seen by any remote, the air and water vehicle pause for any remote, and a lock per guest.
namespace notoriety_thunks {

// With trace=1 the thunks log which remote was seen, which sits in a vehicle and every lock a guest sets.
void setTrace(bool trace);

// {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace notoriety_thunks
