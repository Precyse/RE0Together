#pragma once
// Installs a set of names.h rows as REFramework method hooks, all or nothing (rules H1, H2, H4 of the DD2 design):
// each row is resolved by type, name and parameter types, its body checked against the pinned one, and only when
// every row passes are the hooks added. One line per set: `<set>: N hooked` or `<set>: not hooked (<row>: <reason>)`.
//
// Every handler runs under a fault guard (make): the engine calls hooks outside our tick's guard, so a fault in one is
// logged and dumped (crash_dump::reportCaught) and the game goes on as if the hook were not there (the original runs).
#include <windows.h>

#include <span>

#include "crash_dump.h"
#include "names.h"
#include "reframework/API.h"

namespace hook_registry {

struct Handler {
    const names::MethodRow* row;
    REFPreHookFn pre;    // may be null
    REFPostHookFn post;  // may be null
};

template <REFPreHookFn F>
int guardedPre(int argc, void** argv, REFrameworkTypeDefinitionHandle* types, unsigned long long address) {
    __try {
        return F(argc, argv, types, address);
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "hook")) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }
}

template <REFPostHookFn F>
void guardedPost(void** result, REFrameworkTypeDefinitionHandle type, unsigned long long address) {
    __try {
        F(result, type, address);
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "hook")) {
    }
}

// The one way to make a Handler: the row with its pre and post handlers (either may be nullptr), each behind the guard.
template <REFPreHookFn Pre, REFPostHookFn Post = nullptr>
constexpr Handler make(const names::MethodRow* row) {
    REFPreHookFn pre = nullptr;
    REFPostHookFn post = nullptr;
    if constexpr (Pre != nullptr) pre = &guardedPre<Pre>;
    if constexpr (Post != nullptr) post = &guardedPost<Post>;
    return {row, pre, post};
}

bool installSet(const char* set, std::span<const Handler> handlers);

}  // namespace hook_registry
