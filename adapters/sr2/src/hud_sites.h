#pragma once
#include <span>

#include "partner_sites.h"

// Where milestone 16 wave 2 changes the exe (docs/SR2_NOTES.md, "HUD and waypoints (milestone 16 wave 2)"): the minimap and the
// pause map show one arrow per remote player, the back prompt, the map distance and the waypoint senders know every remote. The
// bytes are what the exe has there. Each table is one set, written all or nothing and only with 3 or more players and the
// milestone 4 partner logic. A Bytes row is only checked: it is the instruction a block resumes at.
namespace hud_sites {

using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// HM1 draws the follower blips of every remote, HM2 and HM3 loop the minimap's partner arrow block (0x7A3A7B to 0x7A3BD8) once
// per remote: HM2 replaces its entry test and resumes at the body (HM2r), HM3 replaces its join and resumes at the body again or
// after the join (HM3r). HM4 leaves every remote out of the NPC blip pass.
inline constexpr Site kMinimap[] = {
    {"HM1", Kind::RetargetCall, 0x7A3A73, "E8 18 BE FF FF", Thunk::EachRemoteFollowerBlips, ""},
    {"HM2", Kind::JumpOut, 0x7A3A97, "E8 34 41 23 00 85 C0 0F 84 34 01 00 00", Thunk::MinimapArrowsBegin, ""},
    {"HM2r", Kind::Bytes, 0x7A3AA4, "A1 88 78 52 02", Thunk::None, ""},
    {"HM3", Kind::JumpOut, 0x7A3BD8, "D9 44 24 10 DC 0D 38 C0 E5 00", Thunk::MinimapArrowsNext, ""},
    {"HM3r", Kind::Bytes, 0x7A3BE2, "D9 5C 24 24", Thunk::None, ""},
    {"HM4", Kind::RetargetCall, 0x79F312, "E8 B9 88 23 00", Thunk::MatchRemoteEsi, ""},
};

// The same loop for the pause map's partner arrow block (0x77028D to 0x77038F).
inline constexpr Site kPauseMap[] = {
    {"HP1", Kind::JumpOut, 0x77028D, "E8 3E 79 26 00 85 C0 0F 84 F5 00 00 00", Thunk::PauseMapArrowsBegin, ""},
    {"HP1r", Kind::Bytes, 0x77029A, "E8 31 79 26 00", Thunk::None, ""},
    {"HP2", Kind::JumpOut, 0x77038F, "A1 E8 5A 5F 02", Thunk::PauseMapArrowsNext, ""},
    {"HP2r", Kind::Bytes, 0x770394, "83 F8 01 74 10", Thunk::None, ""},
};

// HQ1: the getter call of HUD_PRESS_BACK_CANCEL_* (0x4FB4C0) names the remote that holds behaviour 0x22.
inline constexpr Site kBackPrompt[] = {
    {"HQ1", Kind::RetargetCall, 0x4FB57B, "E8 50 C6 4D 00", Thunk::RemoteInBackPromptBehaviour, ""},
};

// HQ2 and HQ3: the two getter calls of the map distance (0x792180) name the remote nearest to the target.
inline constexpr Site kMapDistance[] = {
    {"HQ2", Kind::RetargetCall, 0x7922C6, "E8 05 59 24 00", Thunk::NearestRemoteToStackPoint18, ""},
    {"HQ3", Kind::RetargetCall, 0x7922D3, "E8 F8 58 24 00", Thunk::NearestRemoteToStackPoint18, ""},
};

// GP1 hooks mission_debug, which a script calls with a control text to name the player the next waypoints are sent to; GP2 to GP4
// are the three broadcasts of the waypoint senders (on an object, at a position, remove).
inline constexpr Site kWaypointScope[] = {
    {"GP1", Kind::HookEntry, 0xA398C0, "55 8B EC 83 E4 F8 8B 45", Thunk::WaypointScopeControl, ""},
    {"GP2", Kind::RetargetCall, 0x767945, "E8 F6 CD 0A 00", Thunk::SendWaypointToScope, ""},
    {"GP3", Kind::RetargetCall, 0x7712F3, "E8 48 34 0A 00", Thunk::SendWaypointToScope, ""},
    {"GP4", Kind::RetargetCall, 0x7713B4, "E8 87 33 0A 00", Thunk::SendWaypointToScope, ""},
};

struct Set {
    const char* name;
    std::span<const Site> sites;
};

inline constexpr Set kSets[] = {
    {"minimap", kMinimap},
    {"pause map", kPauseMap},
    {"back prompt", kBackPrompt},
    {"map distance", kMapDistance},
    {"waypoint scope", kWaypointScope},
};

}  // namespace hud_sites
