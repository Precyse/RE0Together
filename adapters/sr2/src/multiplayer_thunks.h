#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Finds the code behind a site of milestone 9 (multiplayer_sites.h) in the files that hold it.
namespace multiplayer_thunks {

// {nullptr, nullptr} for a thunk none of them knows.
partner_thunks::Binding bindingOf(partner_sites::Thunk thunk);

}  // namespace multiplayer_thunks
