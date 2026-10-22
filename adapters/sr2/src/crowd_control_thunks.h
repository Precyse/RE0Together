#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// The code behind the sites of crowd_control_sites.h (milestone 17 wave 3), split by area: crowd_control_select.cpp (which remote a
// test is about, and the warp) and crowd_control_each.cpp (blocks that run once per remote).
namespace crowd_control_thunks {

// Each returns {nullptr, nullptr} for a thunk that belongs to another file.
partner_thunks::Binding bindSelect(partner_sites::Thunk thunk);
partner_thunks::Binding bindEach(partner_sites::Thunk thunk);

// The binding of a thunk of either file.
inline partner_thunks::Binding bind(partner_sites::Thunk thunk) {
    const partner_thunks::Binding select = bindSelect(thunk);
    return select.entry ? select : bindEach(thunk);
}

}  // namespace crowd_control_thunks
