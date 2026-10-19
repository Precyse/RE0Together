#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// The code behind the walk rows of walk_sites.h (milestone 17 wave 3) that are not the Mayhem counter (that is mayhem_blocks.h): the
// partner getter in place of an inline walk, the Fraud start block once per remote, and the per-remote calls of the Fraud and Snatch
// ends.
namespace walk_thunks {

// {nullptr, nullptr} for a thunk that belongs to another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace walk_thunks
