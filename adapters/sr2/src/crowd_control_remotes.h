#pragma once
#include <cstddef>
#include <cstdint>

#include "partner_rules.h"
#include "partner_scope.h"

// The remote player humans as the Crowd Control thunks (crowd_control_select.cpp, crowd_control_each.cpp) read them.
namespace crowd_control_remotes {

struct List {
    uintptr_t humans[partner_rules::kMaxWalked];
    size_t count = 0;
};

// Every remote player human now, in partner_scope::eachRemoteHuman order, none when there is no remote.
inline List take() {
    List list;
    partner_scope::eachRemoteHuman([&list](uintptr_t human) {
        if (human && list.count < partner_rules::kMaxWalked) list.humans[list.count++] = human;
    });
    return list;
}

}  // namespace crowd_control_remotes
