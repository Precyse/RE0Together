#pragma once
#include <iterator>

#include "partner_sites.h"

// Where milestone 9 changes the exe for the missions (docs/SR2_NOTES.md, "Mission players"). Copied from the research table
// m9_missions_sites.tsv (the C++ rows MC1 and MC2 are in session_sites.h with the other message handlers); the bytes are what
// the exe has there. The rows wait for the live probes T and CP and are written with partner_experimental only.
namespace mission_sites {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// Players are placed apart (MC3, MC5) and a shared car goes to the guest that keeps it (MC4).
inline constexpr Site kPlayers[] = {
    {"MC3", Kind::RetargetCall, Gate::ProbeT, 0x6BEC7D, "E8 BE 5A 15 00", Thunk::SendCutsceneEndPerRemote, ""},
    {"MC4", Kind::RetargetCall, Gate::ProbeCp, 0x6A14C9, "E8 02 67 33 00", Thunk::SharedVehicleHolder, ""},
    {"MC5", Kind::HookEntry, Gate::ProbeT, 0x9DB2E0, "83 EC 38 56 8B 74 24 40", Thunk::SpreadRemoteTeleport, ""},
};

}  // namespace mission_sites
