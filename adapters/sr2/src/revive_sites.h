#pragma once
#include "partner_sites.h"

// Where milestone 16 W2 changes the exe for downed players, revives, the respawn wait and notoriety (docs/SR2_NOTES.md "Downed,
// revive and notoriety (M16 W2)"). The bytes are what the exe has there. Each table is one set, written all or nothing and only
// with 3 or more players and the milestone 9 logic. A Bytes row with no tail only checks the code a thunk resumes at.
namespace revive_sites {

using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// RW1 to RW4: the respawn wait per remote (the death message handler 0x8CBBD0, the local death 0x8CBCB0 and the reset 0x8CBD90).
// RA1 to RA3: the anchor of a revive placement (0x9B04D0). RM1: the mission start resets every remote's downed state. RK1: the
// knockdown of 0x9AC180 skips the shared send for any remote in activity type 0xE.
inline constexpr Site kRevive[] = {
    {"RW1", Kind::RetargetCall, 0x8CBBFB, "E8 B0 01 00 00", Thunk::RemoteDeathWait, ""},
    {"RW2", Kind::JumpOut, 0x8CBC83, "E8 D8 38 0E 00", Thunk::DownedLocalKillGate, ""},
    {"RW2a", Kind::Bytes, 0x8CBC88, "A1 14 8C 52 02", Thunk::None, ""},
    {"RW2b", Kind::Bytes, 0x8CBC49, "5E C3", Thunk::None, ""},
    {"RW3", Kind::RetargetCall, 0x8CBCFB, "E8 B0 00 00 00", Thunk::LocalDeathWait, ""},
    {"RW4", Kind::HookEntry, 0x8CBD90, "51 6A 00 6A 00 E8 96 35", Thunk::RespawnWaitReset, ""},
    {"RA1", Kind::RetargetCall, 0x9B0626, "E8 A5 75 02 00", Thunk::ReviveAnchor, ""},
    {"RA2", Kind::RetargetCall, 0x9B062F, "E8 9C 75 02 00", Thunk::ReviveAnchor, ""},
    {"RA3", Kind::RetargetCall, 0x9B063C, "E8 8F 75 02 00", Thunk::ReviveAnchorPlace, ""},
    {"RM1", Kind::JumpOut, 0x6A025F, "E8 6C 79 33 00", Thunk::MissionStartRemotesDowned, ""},
    {"RM1r", Kind::Bytes, 0x6A02D6, "8B 35 D4 03 17 02", Thunk::None, ""},
    {"RK1", Kind::RetargetCall, 0x9AC7CA, "E8 01 B4 02 00", Thunk::MatchRemoteEsi, ""},
};

// NW1: a crime counts as seen when the team sees any remote. NV1: the notoriety tick pauses while any remote is in an air or water
// vehicle. NL1, NL2: the guest min and max locks (notor_pkt subs 2 and 3) keep one value per guest.
inline constexpr Site kNotoriety[] = {
    {"NW1", Kind::RetargetCall, 0x55E8E1, "E8 DA F5 FF FF", Thunk::NotorietyWitnessAnyRemote, ""},
    {"NV1", Kind::RetargetCall, 0x55F13E, "E8 8D 8A 47 00", Thunk::NotorietyRemoteInAirOrWater, ""},
    {"NL1", Kind::CallBlock, 0x55EF6F, "89 14 85 8C AA 79 02", Thunk::NotorietyGuestMinLock, "90 90"},
    {"NL2", Kind::CallBlock, 0x55EF04, "89 04 8D 94 AA 79 02", Thunk::NotorietyGuestMaxLock, "90 90"},
};

}  // namespace revive_sites
