#pragma once
#include <span>

#include "partner_sites.h"

// Where milestone 17 wave 3 changes the exe (docs/SR2_NOTES.md, "Remote poses, world walks, garage door and client name (milestone 17
// wave 3)"): the tagging and CPR poses of every remote, the ambient spawn view of every remote, a follower bar for every remote, the
// activity start warp, the crib garage door, the Hitman view cone and the {CLIENT_NAME} text. Copied from the research table of
// M17_W3_WORLD.md; the bytes are what the exe has there. Each table is one set, written all or nothing and only with 3 or more
// players and the milestone 9 logic (the activity warp gate also needs the milestone 7 logic). A Bytes row is only checked: it is
// the instruction a block goes on at.
namespace world_walks_sites {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// PD1 lets any remote copy through the pose gate of the anim state select 0x96EF10, so its own tagging flag (+0x1161 bit 0x20) picks
// the tagging pose. PD2 replaces the CPR pose test with the pose stored for that remote: the call, then `cmp eax, -1; je PD2b`, and
// nops into PD2a, which returns eax. PD3 records the pose each sender's act 0x4B (the Ambulance CPR) sets.
inline constexpr Site kRemotePoses[] = {
    {"PD1", Kind::RetargetCall, Gate::Now, 0x96F197, "E8 34 8A 06 00", Thunk::MatchRemoteEdi, ""},
    {"PD2", Kind::CallBlock, Gate::Now, 0x96F1B3, "E8 38 8C C8 FF 84 C0 74 0A A1 8C 7C E9 00", Thunk::AmbulancePoseOfEdi,
     "83 F8 FF 74 09 90 90 90 90"},
    {"PD2a", Kind::Bytes, Gate::Now, 0x96F1C1, "5E 5D 5B 59 C3", Thunk::None, ""},
    {"PD2b", Kind::Bytes, Gate::Now, 0x96F1C6, "8B C7 8B CD E8 01 FA FF FF", Thunk::None, ""},
    {"PD3", Kind::PointerSwap, Gate::Now, 0xE8AB54, "70 7E 5F 00", Thunk::RecordAmbulancePose, ""},
};

// SW1: the ambient spawn candidate is rejected when 0x936080 says a human sees it or is near it; it asks every remote.
inline constexpr Site kSpawnView[] = {
    {"SW1", Kind::RetargetCall, Gate::Now, 0x93B974, "E8 07 A7 FF FF", Thunk::AnyRemoteSeesSpawnPoint, ""},
};

// FB1 is the join of every path of the follower bars walk of the local HUD (0x797CD0); the stub fixes the bars and goes on at FB1r.
inline constexpr Site kFollowerBars[] = {
    {"FB1", Kind::JumpOut, Gate::Now, 0x797DEE, "33 F6 89 74 24 20", Thunk::FollowerBarsEachRemote, ""},
    {"FB1r", Kind::Bytes, Gate::Now, 0x797DF4, "8B 0C B5 74 85 52 02", Thunk::None, ""},
};

// AW1: the presence test of the activity start warp (0x53E2A0) names the first remote that is not down.
inline constexpr Site kActivityWarpGate[] = {
    {"AW1", Kind::RetargetCall, Gate::Now, 0x53E371, "E8 5A 98 49 00", Thunk::FreeRemoteForActivityWarp, ""},
};

// GZ1: the crib garage door (0x554BD0) waits for the remote that stands in one of the garage's boxes.
inline constexpr Site kGarageDoor[] = {
    {"GZ1", Kind::RetargetCall, Gate::Now, 0x554D5F, "E8 6C 2E 48 00", Thunk::RemoteInGarageBoxes, ""},
};

// VF1: the view cone of a human (0x5E3E40) reads the aim flag of the human asked about, whoever that remote is.
inline constexpr Site kViewCone[] = {
    {"VF1", Kind::RetargetCall, Gate::Now, 0x5E3E94, "E8 37 3D 3F 00", Thunk::MatchRemoteEsi, ""},
};

// CN1 remembers the sender of misc 7 (the client's activity completion masks); CN2 makes {CLIENT_NAME} name that sender.
inline constexpr Site kClientName[] = {
    {"CN1", Kind::PointerSwap, Gate::Now, 0xE8ABB4, "60 A6 8C 00", Thunk::RememberCompletionSender, ""},
    {"CN2", Kind::RetargetCall, Gate::Now, 0x7EDBA9, "E8 22 A0 1E 00", Thunk::ClientNameHuman, ""},
};

struct Set {
    const char* name;
    std::span<const Site> sites;
    bool needsActivities;  // written only with the milestone 7 activity logic
};

inline constexpr Set kSets[] = {
    {"remote poses", kRemotePoses, false},
    {"spawn view", kSpawnView, false},
    {"follower bars", kFollowerBars, false},
    {"activity warp gate", kActivityWarpGate, true},
    {"garage door", kGarageDoor, false},
    {"view cone", kViewCone, false},
    {"client name", kClientName, false},
};

}  // namespace world_walks_sites
