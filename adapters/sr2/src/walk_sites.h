#pragma once
#include <span>

#include "partner_sites.h"

// Where milestone 17 wave 3 changes the exe for the inline walks of the first remote and the Mayhem counter with 3 or 4 players
// (docs/SR2_NOTES.md, "Walks and the Mayhem counter (milestone 17 wave 3)"). Copied from the research spec M17_W3_WALKS.md section 4;
// the bytes are what the exe has there. Each table is one set, written all or nothing; a set whose bytes differ is left out and the
// others still go in.
namespace walk_sites {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// Insurance Fraud: the remote start block, the damage multiplier, the district start, the second time-up path and the ragdoll end for every remote (DWF1 to DWF7).
inline constexpr Site kFraudWalks[] = {
    {"DWF1", Kind::JumpOut, Gate::Now, 0x64CCCE, "83 7C 24 10 00 0F 84 80 06 00 00", Thunk::FraudStartBlockBegin, ""},
    {"DWF2", Kind::JumpOut, Gate::Now, 0x64D359, "8B 94 24 58 0A 00 00", Thunk::FraudStartBlockNext, ""},
    {"DWF3", Kind::RetargetCall, Gate::Now, 0x64E193, "E8 F8 A4 31 00", Thunk::EachRemoteDamageReset, ""},
    {"DWF4", Kind::CallBlock, Gate::Now, 0x64E6B5, "8A 0D 49 8B 52 02 84 C9 74 24 8B 0D 3C 04 17 02 8B C1 3B C5 74 18 8B 15 D4 03 17 02 3B C2 75 10 8B 80 94 12 00 00 3B C1 74 04 3B C5 75 EE 33 C0", Thunk::AnyRemoteInFraudDistrict, "EB 29 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90"},
    {"DWF5", Kind::RetargetCall, Gate::Now, 0x64E861, "E8 DA 76 31 00", Thunk::EachRemote965F40, ""},
    {"DWF6", Kind::JumpOut, Gate::Now, 0x64E8B0, "8B 0D 3C 04 17 02 8B C1 3B C5 74 3A 8B 15 D4 03 17 02 3B C2 75 10 8B 80 94 12 00 00 3B C1 74 26 3B C5 75 EE EB 20 3B C5 74 1C 80 B8 95 05 00 00 00 74 13 F6 80 84 00 00 00 01 74 0A 55 50 E8 9D E3 35 00 83 C4 08", Thunk::EachRemoteRagdollEnd, ""},
    {"DWF7", Kind::CallBlock, Gate::Now, 0x651EB9, "A1 3C 04 17 02 8B F0", Thunk::OwnerRemoteEdi, "8B F0"},
};

// Snatch: the cleanup for every remote, the delivery stat for whoever delivers (DWP1 to DWP4).
inline constexpr Site kSnatchWalks[] = {
    {"DWP1", Kind::JumpOut, Gate::Now, 0x656D3F, "6A 01 8B CE E8 C8 75 38 00 83 C4 04 6A 00 56 E8 FD 95 38 00", Thunk::EachRemoteSnatchCleanup, ""},
    {"DWP2", Kind::CallBlock, Gate::Now, 0x657D45, "8A 15 49 8B 52 02 84 D2 8B 15 D4 03 17 02 74 1E 8B 0D 3C 04 17 02 8B C1 85 C0 74 12 3B C2 75 10 8B 80 94 12 00 00 3B C1 74 04 85 C0 75 EE 33 C0", Thunk::MatchRemoteEsi, "8B 15 D4 03 17 02 EB 23 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90"},
    {"DWP3", Kind::RetargetCall, Gate::Now, 0x657D89, "E8 92 D1 05 00", Thunk::SnatchStatToDeliverer, ""},
    {"DWP4", Kind::RetargetCall, Gate::Now, 0x6B507E, "E8 BD F6 15 00", Thunk::SendStatToTarget, ""},
};

// Trafficking: the init dismisses the followers and arms the weapons of every remote (DWK1, DWK2).
inline constexpr Site kTraffickingWalks[] = {
    {"DWK1", Kind::RetargetCall, Gate::Now, 0x65F7EC, "E8 6F FC 32 00", Thunk::EachRemoteDismissFollowers, ""},
    {"DWK2", Kind::RetargetCall, Gate::Now, 0x65F838, "E8 73 79 00 00", Thunk::EachRemoteTraffickingArm, ""},
};

// Mayhem: the kill message names the remote that sent it (DWM1).
inline constexpr Site kMayhemWalks[] = {
    {"DWM1", Kind::CallBlock, Gate::Now, 0x66D431, "A0 49 8B 52 02 84 C0 74 28 8B 0D 3C 04 17 02 8B C1 85 C0 74 1C 8B 15 D4 03 17 02 8D 64 24 00 3B C2 75 10 8B 80 94 12 00 00 3B C1 74 04 85 C0 75 EE 33 C0", Thunk::InlineWalkGetter, "EB 2C 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90"},
};

// Demolition derby: the walks that name a participant take the pinned partner (SWD1 to SWD4).
inline constexpr Site kDerbyWalks[] = {
    {"SWD1", Kind::CallBlock, Gate::Now, 0x5E9A58, "A0 49 8B 52 02 84 C0 74 24 8B 0D 3C 04 17 02 8B C1 3B C3 74 18 8B 15 D4 03 17 02 3B C2 75 10 8B 80 94 12 00 00 3B C1 74 04 3B C3 75 EE 33 C0", Thunk::InlineWalkGetter, "EB 28 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90"},
    {"SWD2", Kind::CallBlock, Gate::Now, 0x5E9C79, "8A 0D 49 8B 52 02 84 C9 74 24 8B 0D 3C 04 17 02 8B C1 3B C3 74 18 8B 15 D4 03 17 02 3B C2 75 10 8B 80 94 12 00 00 3B C1 74 04 3B C3 75 EE 33 C0", Thunk::InlineWalkGetter, "EB 29 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90"},
    {"SWD3", Kind::CallBlock, Gate::Now, 0x5EDA84, "A0 49 8B 52 02 84 C0 74 24 8B 0D 3C 04 17 02 8B C1 3B C3 74 18 8B 15 D4 03 17 02 3B C2 75 10 8B 80 94 12 00 00 3B C1 74 04 3B C3 75 EE 33 C0", Thunk::InlineWalkGetter, "EB 28 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90"},
    {"SWD4", Kind::CallBlock, Gate::Now, 0x5EDABF, "8A 0D 49 8B 52 02 84 C9 74 24 8B 0D 3C 04 17 02 8B C1 3B C3 74 18 8B 15 D4 03 17 02 3B C2 75 10 8B 80 94 12 00 00 3B C1 74 04 3B C3 75 EE 33 C0", Thunk::InlineWalkGetter, "EB 29 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90"},
};

// Fight club: the start and end blocks of the participant (SWF1, SWF2).
inline constexpr Site kFightClubWalks[] = {
    {"SWF1", Kind::CallBlock, Gate::Now, 0x5DB1E9, "8B 0D 3C 04 17 02 8B C1 85 C0 0F 84 1E FF FF FF 8B 15 D4 03 17 02 90 3B C2 75 17 8B 80 94 12 00 00 3B C1 0F 84 05 FF FF FF 85 C0 75 EA E9 FC FE FF FF", Thunk::InlineWalkGetter, "EB 2B 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90"},
    {"SWF2", Kind::CallBlock, Gate::Now, 0x5DCC0A, "8A 15 49 8B 52 02 84 D2 8B 0D D4 03 17 02 74 2C 8B 15 3C 04 17 02 8B C2 85 C0 74 20 8B 35 D4 03 17 02 8D 64 24 00 3B C6 0F 85 B7 00 00 00 8B 80 94 12 00 00 3B C2 74 04 85 C0 75 EA 33 F6", Thunk::InlineWalkGetter, "8B F0 8B 0D D4 03 17 02 EB 2F 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90"},
};

// Escort: init, start and end of the participant (SWE1 to SWE3).
inline constexpr Site kEscortWalks[] = {
    {"SWE1", Kind::CallBlock, Gate::Now, 0x671360, "8A 0D 49 8B 52 02 84 C9 74 28 8B 0D 3C 04 17 02 8B C1 85 C0 74 1C 8B 15 D4 03 17 02 8D 64 24 00 3B C2 75 10 8B 80 94 12 00 00 3B C1 74 04 85 C0 75 EE 33 C0", Thunk::InlineWalkGetter, "EB 2D 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90"},
    {"SWE2", Kind::CallBlock, Gate::Now, 0x671994, "8B 0D 3C 04 17 02 8B C1 3B C7 74 1C 8B 15 D4 03 17 02 3B C2 0F 85 F3 00 00 00 8B 80 94 12 00 00 3B C1 74 04 3B C7 75 EA", Thunk::InlineWalkGetter, "8B D8 EB 21 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90"},
    {"SWE3", Kind::CallBlock, Gate::Now, 0x67227C, "8B 15 3C 04 17 02 8B C2 85 C0 74 1A 8B 35 D4 03 17 02 8B FF 3B C6 75 32 8B 80 94 12 00 00 3B C2 74 04 85 C0 75 EE", Thunk::InlineWalkGetter, "8B F0 8B 0D D4 03 17 02 EB 19 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90"},
};

// Mayhem per-player counters and score blocks: the init, the two destruction counts, the bonus loop and the block lookup (MB0 to MB4).
inline constexpr Site kMayhemBlocks[] = {
    {"MB0", Kind::CallBlock, Gate::Now, 0x66B1BB, "C7 83 70 05 00 00 00 00 00 00", Thunk::MayhemBlocksInit, "90 90 90 90 90"},
    {"MB1", Kind::CallBlock, Gate::Now, 0x66C500, "83 44 90 5C 01", Thunk::MayhemCountDestruction, ""},
    {"MB2", Kind::CallBlock, Gate::Now, 0x66C56F, "83 44 90 5C 01", Thunk::MayhemCountDestruction, ""},
    {"MB3", Kind::JumpOut, Gate::Now, 0x66BB88, "83 C6 04 83 6C 24 14 01 0F 85 5C FF FF FF", Thunk::MayhemPayRemotes, ""},
    {"MB4", Kind::CallBlock, Gate::Now, 0x66C6BC, "8D 04 C0 C1 E0 06 8D 9C 30 B4 00 00 00", Thunk::MayhemScoreBlock, "90 90 90 90 90 90 90 90"},
};

struct Set {
    const char* name;
    std::span<const Site> sites;
};

inline constexpr Set kSets[] = {
    {"fraud walks", kFraudWalks},   {"snatch walks", kSnatchWalks}, {"trafficking walks", kTraffickingWalks}, {"mayhem walks", kMayhemWalks},
    {"derby walks", kDerbyWalks},   {"fight club walks", kFightClubWalks}, {"escort walks", kEscortWalks},    {"mayhem blocks", kMayhemBlocks},
};

}  // namespace walk_sites
