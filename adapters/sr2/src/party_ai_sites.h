#pragma once
#include <span>

#include "partner_sites.h"

// Where milestone 16 wave 2 changes the exe for homies, followers and NPC AI (docs/SR2_NOTES.md, "Homies, followers and NPC AI
// (milestone 16 wave 2)"). Copied from section 2 of the research spec M16_W2_PARTY.md; the bytes are what the exe has there. A Bytes
// row with an empty tail is a check of a function or a resume point the thunks use. Each table is a phase, written all or nothing and
// only with 3 or more players and the milestone 4 partner logic.
namespace party_ai_sites {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// PA1: the second leader of a leader's extra follower goes round the remotes (the call of get_remote_player in 0x98EC10). PA2 and
// PA3: the mode enter 0x53B480 dismisses the followers (derby) and clears the attachment (Septic Avenger) of every remote.
inline constexpr Site kParty[] = {
    {"PA1", Kind::RetargetCall, Gate::Now, 0x98EEBA, "E8 11 8D 04 00", Thunk::SecondLeaderShare, ""},
    {"PA2", Kind::CallBlock, Gate::Now, 0x53B723,
     "E8 A8 C4 49 00 85 C0 74 18 53 E8 9E C4 49 00 8B F8 E8 27 3D 45 00 83 C4 04", Thunk::DismissEachRemoteFollowers, "EB 1A"},
    {"PA3", Kind::CallBlock, Gate::Now, 0x53B66F,
     "3B FB 74 1B 8B 87 38 03 00 00 3B C3 74 11 88 58 24 89 58 10 8B 87 38 03 00 00 E8 52 08 03 00",
     Thunk::ClearEachRemoteAttachment, "EB 18"},
    {"PAc1", Kind::Bytes, Gate::Now, 0x98F460, "81 EC 24 05 00 00 A1 50", Thunk::None, ""},
    {"PAc2", Kind::Bytes, Gate::Now, 0x56BEE0, "80 60 18 F7 56 8B 35 38", Thunk::None, ""},
    {"PAc3", Kind::Bytes, Gate::Now, 0x53B744, "8B 45 00 05 0C 01 00 00", Thunk::None, ""},
    {"PAc4", Kind::Bytes, Gate::Now, 0x53B68E, "E8 8D 05 03 00", Thunk::None, ""},
    {"PAc5", Kind::Bytes, Gate::Now, 0x985630, "8B 54 24 04 85 D2 74 36", Thunk::None, ""},
};

// PT1 (trace=1 only): the follower add 0x98EC10, to see the tick's hand-over (PTc1, the call that returns to 0x98F7C0).
inline constexpr Site kPartyTrace[] = {
    {"PT1", Kind::HookEntry, Gate::Now, 0x98EC10, "55 8B EC 83 E4 F8 81 EC", Thunk::TraceFollowerAdd, ""},
    {"PTc1", Kind::Bytes, Gate::Now, 0x98F7BB, "E8 50 F4 FF FF", Thunk::None, ""},
};

// AI1, AI2: the two "nobody sees it" ray tests (0x4294C0). AI3, AI4: the behaviour 3 target of 0x405C70. AI5: the range test of
// 0x44E800. AI6: the facing test of 0x4510C0. AI7: the distance of the ambient reactions 0x44AD10.
inline constexpr Site kNpcSight[] = {
    {"AI1", Kind::RetargetCall, Gate::Now, 0x4295F9, "E8 C2 FE FF FF", Thunk::EveryRemoteRayBlocked, ""},
    {"AI2", Kind::RetargetCall, Gate::Now, 0x429698, "E8 23 FE FF FF", Thunk::EveryRemoteRayBlocked, ""},
    {"AI3", Kind::RetargetCall, Gate::Now, 0x405C90, "E8 3B 1F 5D 00", Thunk::ScopedOrNearestRemoteEdi14, ""},
    {"AI4", Kind::RetargetCall, Gate::Now, 0x405C9D, "E8 2E 1F 5D 00", Thunk::ScopedOrNearestRemoteEdi14, ""},
    {"AI5", Kind::RetargetCall, Gate::Now, 0x44E8D5, "E8 F6 92 58 00", Thunk::NearestRemoteEbp14, ""},
    {"AI6", Kind::RetargetCall, Gate::Now, 0x451114, "E8 E7 FE FF FF", Thunk::AnyRemoteInView, ""},
    {"AI7", Kind::RetargetCall, Gate::Now, 0x44ADCF, "E8 5C 3C 79 00", Thunk::NearestRemoteSquaredDistance, ""},
    {"AIc1", Kind::Bytes, Gate::Now, 0x4294C0, "83 EC 68 53 56 33 F6 3B", Thunk::None, ""},
    {"AIc2", Kind::Bytes, Gate::Now, 0x451000, "83 EC 14 85 F6 0F 84 A3", Thunk::None, ""},
    {"AIc3", Kind::Bytes, Gate::Now, 0xBDEA30, "83 EC 08 D9 01 D8 20 D9", Thunk::None, ""},
};

// AI8: the spawn record team test of 0x47A650. AI9: the damage scale of 0x9722D0. AI10, AI11: the follower of the target in 0x9BC930.
inline constexpr Site kNpcTeam[] = {
    {"AI8", Kind::RetargetCall, Gate::Now, 0x47A688, "E8 43 D5 55 00", Thunk::RemoteOfRecordTeamEsi, ""},
    {"AI9", Kind::RetargetCall, Gate::Now, 0x97245D, "E8 6E 57 06 00", Thunk::RemoteOnTeamOfEbp, ""},
    {"AI10", Kind::RetargetCall, Gate::Now, 0x9BCDC9, "E8 02 AE 01 00", Thunk::RemoteLeaderOfEsi, ""},
    {"AI11", Kind::RetargetCall, Gate::Now, 0x9BCDD2, "E8 F9 AD 01 00", Thunk::RemoteLeaderOfEsi, ""},
    {"AIc4", Kind::Bytes, Gate::Now, 0x421740, "56 57 8B F8 E8 77 D0 56", Thunk::None, ""},
};

// AI12: the outnumbered-damage odds 0x9BC5D0 count the team of every player, not one other player.
inline constexpr Site kTeamOdds[] = {
    {"AI12", Kind::HookEntry, Gate::Now, 0x9BC5D0, "83 EC 18 55 8B 6C 24 20", Thunk::TeamOddsEveryPlayer, ""},
    {"AIc5", Kind::Bytes, Gate::Now, 0x98E750, "85 D2 75 03 33 C0 C3 8B", Thunk::None, ""},
    {"AIc6", Kind::Bytes, Gate::Now, 0x43A620, "0F BE 88 49 01 00 00 85", Thunk::None, ""},
    {"AIc7", Kind::Bytes, Gate::Now, 0x43A190, "53 55 8B 6C 24 0C 56 8B", Thunk::None, ""},
    {"AIc8", Kind::Bytes, Gate::Now, 0x55AE30, "83 EC 08 80 7C 24 10 00", Thunk::None, ""},
    {"AIc9", Kind::Bytes, Gate::Now, 0x404A20, "83 EC 08 D9 01 D8 20 D9", Thunk::None, ""},
    {"AIc10", Kind::Bytes, Gate::Now, 0x985630, "8B 54 24 04 85 D2 74 36", Thunk::None, ""},
};

struct Phase {
    const char* name;
    std::span<const Site> sites;
    bool traceOnly;  // written only with trace=1
};

inline constexpr Phase kPhases[] = {
    {"party", kParty, false},
    {"party trace", kPartyTrace, true},
    {"npc sight and range", kNpcSight, false},
    {"npc team", kNpcTeam, false},
    {"team odds", kTeamOdds, false},
};

}  // namespace party_ai_sites
