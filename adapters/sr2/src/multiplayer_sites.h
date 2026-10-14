#pragma once
#include <span>

#include "partner_sites.h"
#include "session_sites.h"

// The phases of milestone 9 (docs/SR2_NOTES.md): each table is byte-checked and written all or nothing.
namespace multiplayer_sites {

struct Phase {
    const char* name;
    std::span<const partner_sites::Site> sites;
    bool traceOnly;  // written only with trace=1
};

inline constexpr Phase kPhases[] = {
    {"sender scope", session_sites::kScope, false},
    {"looks", session_sites::kLooks, false},
};

}  // namespace multiplayer_sites
