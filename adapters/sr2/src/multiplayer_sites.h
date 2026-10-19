#pragma once
#include <span>

#include "mission_sites.h"
#include "partner_sites.h"
#include "player_sites.h"
#include "session_sites.h"
#include "trace_sites.h"
#include "world_sites.h"

// The phases of milestone 9 (docs/SR2_NOTES.md): each table is byte-checked and written all or nothing.
namespace multiplayer_sites {

struct Phase {
    const char* name;
    std::span<const partner_sites::Site> sites;
    bool traceOnly;  // written only with trace=1
    bool needsLeavePath = false;  // runs the leave pass, which needs the milestone 5i edits (player_cap)
};

inline constexpr Phase kPhases[] = {
    {"sender scope", session_sites::kScope, false},
    {"looks", session_sites::kLooks, false},
    {"guest leave", session_sites::kGuestLeave, false, true},
    {"pause flag", session_sites::kPause, false},
    {"cutscene handshake", session_sites::kCutscene, false},


    {"join admission", session_sites::kAdmission, false},
    {"player traces", trace_sites::kPlayers, true},
    {"net clock", player_sites::kNetClock, false},
    {"bundle sender trace", player_sites::kBundleSender, true},
    {"census trace", trace_sites::kCensus, true},

    {"population budgets", world_sites::kBudget, false},

    {"population cluster", world_sites::kCluster, false},
    {"population partner", world_sites::kPartnerView, false},
    {"remote views", world_sites::kViews, false},
    {"mission players", mission_sites::kPlayers, false},
    {"remote vehicles", world_sites::kRemoteVehicles, false},
    {"creation queue", world_sites::kQueue, false},
    {"upload share", world_sites::kUpload, false},



};

}  // namespace multiplayer_sites
