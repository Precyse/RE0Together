#pragma once
// Installs a set of names.h rows as REFramework method hooks, all or nothing (rules H1, H2, H4 of the DD2 design):
// each row is resolved by type, name and parameter types, its body checked against the pinned one, and only when
// every row passes are the hooks added. One line per set: `<set>: N hooked` or `<set>: not hooked (<row>: <reason>)`.
#include <span>

#include "names.h"
#include "reframework/API.h"

namespace hook_registry {

struct Handler {
    const names::MethodRow* row;
    REFPreHookFn pre;    // may be null
    REFPostHookFn post;  // may be null
};

bool installSet(const char* set, std::span<const Handler> handlers);

}  // namespace hook_registry
