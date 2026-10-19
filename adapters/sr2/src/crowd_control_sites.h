#pragma once
#include <span>

#include "partner_sites.h"

// Where milestone 17 wave 3 changes the exe for Crowd Control with 3 or 4 players (docs/SR2_NOTES.md, "Crowd Control (milestone 17
// wave 3)"). Copied from the research spec M17_W3_CROWD.md section 4; the bytes are what the exe has there. Each table is one set,
// written all or nothing; a set whose bytes differ is left out and the others still go in. A jump out has no tail: it replaces
// its block with a jump to the thunk and nops, and the thunk resumes the game at the address after the block.
namespace crowd_control_sites {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// The celebrity held by a remote: the fan test and the grab release answer the remote that holds it (CR1 to CR6).
inline constexpr Site kHold[] = {
    {"CR1", Kind::RetargetCall, Gate::Now, 0x5A4594, "E8 37 36 43 00", Thunk::RemoteHoldingEdi, ""},
    {"CR2", Kind::RetargetCall, Gate::Now, 0x5A45A1, "E8 2A 36 43 00", Thunk::RemoteHoldingEdi, ""},
    {"CR3", Kind::RetargetCall, Gate::Now, 0x5A46AC, "E8 1F 35 43 00", Thunk::RemoteHoldingEdi, ""},
    {"CR4", Kind::RetargetCall, Gate::Now, 0x5A46B9, "E8 12 35 43 00", Thunk::RemoteHoldingEdi, ""},
    {"CR5", Kind::RetargetCall, Gate::Now, 0x5A46CD, "E8 FE 34 43 00", Thunk::RemoteHoldingEdi, ""},
    {"CR6", Kind::RetargetCall, Gate::Now, 0x5A46E1, "E8 EA 34 43 00", Thunk::RemoteHoldingEdi, ""},
};

// Phases: the object and start waits and the protect start answer every remote (CR7 to CR9).
inline constexpr Site kPhases[] = {
    {"CR7", Kind::RetargetCall, Gate::Now, 0x5AA9E1, "E8 EA D1 42 00", Thunk::NearestRemoteToCelebCarEsi, ""},
    {"CR8", Kind::RetargetCall, Gate::Now, 0x5AB596, "E8 35 C6 42 00", Thunk::NearestRemoteToRemoteStart, ""},
    {"CR9", Kind::JumpOut, Gate::Now, 0x5A1B30, "33 F6 8B 7C B4 10 85 FF 74 13 83 BF 74 0D 00 00 00 74 0A 6A 01 E8 76 5B 56 00 83 C4 04 83 C6 01 83 FE 03 7C DD", Thunk::EachPlayerProtectStart, ""},
};

// The level start: weapon, flags, followers and warp for every remote (CR10 to CR13).
inline constexpr Site kStart[] = {
    {"CR10", Kind::JumpOut, Gate::Now, 0x5AD870, "8B 0D 3C 04 17 02 8B C1 85 C0 74 64 8B 15 D4 03 17 02 3B C2 75 10 8B 80 94 12 00 00 3B C1 74 50 85 C0 75 EE EB 4A 85 C0 74 46 8A 0D 49 8B 52 02 84 C9 74 24 8B 0D 3C 04 17 02 8B C1 85 C0 74 18 8B 15 D4 03 17 02 3B C2 75 10 8B 80 94 12 00 00 3B C1 74 04 85 C0 75 EE 33 C0 8B 53 74 6A 00 6A 00 6A FF 6A 01 52 8B C8 E8 13 44 5C 00 83 C4 14", Thunk::EachRemoteCrowdWeapon, ""},
    {"CR11", Kind::JumpOut, Gate::Now, 0x5AD912, "8B 0D 3C 04 17 02 8B C1 85 C0 0F 84 A2 00 00 00 8B 15 D4 03 17 02 3B C2 75 17 8B 80 94 12 00 00 3B C1 0F 84 8A 00 00 00 85 C0 75 EA E9 81 00 00 00 85 C0 0F 84 79 00 00 00 A0 49 8B 52 02 84 C0 74 24 8B 0D 3C 04 17 02 8B C1 85 C0 74 18 8B 15 D4 03 17 02 3B C2 75 10 8B 80 94 12 00 00 3B C1 74 04 85 C0 75 EE 33 C0 6A 01 50 E8 8E 2D 43 00 8A 0D 49 8B 52 02 84 C9 74 26 8B 0D 3C 04 17 02 8B C1 85 C0 74 1A 8B 15 D4 03 17 02 8B FF 3B C2 75 10 8B 80 94 12 00 00 3B C1 74 04 85 C0 75 EE 33 C0 6A 00 6A 01 56 05 C4 10 00 00 50 E8 6C CF FA FF", Thunk::EachRemoteCrowdStartFlags, ""},
    {"CR12", Kind::RetargetCall, Gate::Now, 0x5ADA11, "E8 4A 1A 3E 00", Thunk::EachRemoteDismissFollowers, ""},
    {"CR13", Kind::RetargetCall, Gate::Now, 0x5ADA78, "E8 03 DA 42 00", Thunk::TeleportLocalOrEachRemote, ""},
};

// The level end: the release, flags and warp for every remote (CR14 to CR16).
inline constexpr Site kEnd[] = {
    {"CR14", Kind::JumpOut, Gate::Now, 0x5ADF00, "85 FF 74 15 83 BF A8 0E 00 00 00 74 0C 6A 01 8B CF E8 2A 43 40 00 83 C4 04", Thunk::EachRemoteReleaseHeld, ""},
    {"CR15", Kind::JumpOut, Gate::Now, 0x5AE17C, "8B 0D 3C 04 17 02 8B C1 85 C0 0F 84 98 00 00 00 8B 15 D4 03 17 02 3B C2 75 14 8B 80 94 12 00 00 3B C1 0F 84 80 00 00 00 85 C0 75 EA EB 7A 85 C0 74 76 8A 0D 49 8B 52 02 84 C9 74 2A 8B 0D 3C 04 17 02 8B C1 85 C0 74 1E 8B 15 D4 03 17 02 8D 9B 00 00 00 00 3B C2 75 10 8B 80 94 12 00 00 3B C1 74 04 85 C0 75 EE 33 C0 6A 00 50 E8 24 25 43 00 8A 15 49 8B 52 02 84 D2 74 24 8B 0D 3C 04 17 02 8B C1 85 C0 74 18 8B 15 D4 03 17 02 3B C2 75 10 8B 80 94 12 00 00 3B C1 74 04 85 C0 75 EE 33 C0 6A 00 50 E8 BC 94 3B 00", Thunk::EachRemoteCrowdEndFlags, ""},
    {"CR16", Kind::RetargetCall, Gate::Now, 0x5AE6B6, "E8 C5 CD 42 00", Thunk::TeleportLocalOrEachRemote, ""},
};

struct Set {
    const char* name;
    std::span<const Site> sites;
};

inline constexpr Set kSets[] = {
    {"hold", kHold}, {"phases", kPhases}, {"start", kStart}, {"end", kEnd},
};

}  // namespace crowd_control_sites
