#pragma once
#include "partner_sites.h"

// The code behind the sites of partner_sites.h, split by area: partner_thunks_calls.cpp (identity, nearest and
// per-remote call replacements), partner_thunks_session.cpp (getters, join, leave, chunks, messages) and
// partner_thunks_gameplay.cpp (down, vehicles, mission blocks, Lua natives, the experimental thunks).
namespace partner_thunks {

// `entry` is the address a site's branch or hook goes to. For a hook, `original` receives the trampoline that runs
// the function as the game has it.
struct Binding {
    void* entry;
    void** original;
};

// Each returns {nullptr, nullptr} for a thunk that belongs to another file.
Binding bindCalls(partner_sites::Thunk thunk);
Binding bindSession(partner_sites::Thunk thunk);
Binding bindGameplay(partner_sites::Thunk thunk);

}  // namespace partner_thunks
