#pragma once
#include <cstdint>
#include <iterator>
#include <string_view>

#include "partner_sites.h"

// Where milestone 11 changes or reads the exe for the players area (docs/SR2_NOTES.md, "Fast fire"). Copied from the
// research table of M11_SHATTERED_IDEAS.md; the bytes are what the exe has there. Each table is a phase, patched all or
// nothing, and only with 3 or more players.
namespace player_sites {

using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// A guest's net clock never steps back and only takes the host's clock (CK1, CK2).
inline constexpr Site kNetClock[] = {
    {"CK1", Kind::HookEntry, 0x7FCBF0, "8B 44 24 08 83 78 04 00", Thunk::NetClockMessage, ""},
    {"CK2", Kind::HookEntry, 0x7FC930, "83 EC 14 80 3D 56 8B 52 02 00", Thunk::NetClockTick, ""},
};

// trace=1 only: which sender a player bundle packet came from, against the owner of the object it updates (BUT).
inline constexpr Site kBundleSender[] = {
    {"BUT", Kind::PointerSwap, 0xE2B454, "D0 69 86 00", Thunk::TraceBundleSender, ""},
};

// A cutscene hides and restores every remote player (milestone 14, PV1 and PV2): the start hid the first remote in list order and the
// end restored the default partner, so the newest remote stayed hidden. The rows PV1r and PV2r check the code the thunks resume at.
inline constexpr Site kCutsceneVisuals[] = {
    {"PV1", Kind::JumpOut, 0x6C04FD, "A1 3C 04 17 02 8B F0", Thunk::HideRemotesAtCutsceneStart, ""},
    {"PV1r", Kind::Bytes, 0x6C0584, "8B 0D B0 8D E9 00", Thunk::None, ""},
    {"PV2", Kind::JumpOut, 0x6C561B,
     "E8 B0 25 31 00 33 FF 3B C7 74 16 83 A0 80 00 00 00 EB 50 E8 BD A3 FF FF 8B 1D 10 7D 52 02 83 C4 04",
     Thunk::RestoreRemotesAtCutsceneEnd, ""},
    {"PV2r", Kind::Bytes, 0x6C563C, "80 BB FC 0A 00 00 00", Thunk::None, ""},
};

// trace=1 only: the chunk streamed anim sets (milestone 14, AN1 to AN3; docs/SR2_NOTES.md, "Script errors and fades").
inline constexpr Site kAnimTrace[] = {
    {"AN1", Kind::HookEntry, 0xA83F30, "81 EC 28 06 00 00 A1 50", Thunk::TraceStreamingSync, ""},
    {"AN2", Kind::HookEntry, 0xA841C0, "55 8B EC 83 E4 F8 81 EC", Thunk::TraceAnimUnload, ""},
    {"AN3", Kind::HookEntry, 0xCE4870, "81 EC 34 03 00 00 A1 50", Thunk::TraceAnimDelta, ""},
};

// The skin composite pool (milestone 14, PV3 to PV10) holds 2 objects of 0x78 bytes at 0x21F2988; its build, its device lost release
// and its recreation reach it through eight immediates: the base, the end (base + count x 0x78) and the same two moved by 0x24 and
// 0x4C into the object. The row i takes value i of `player_visuals_rules::skinPoolImmediates`.
struct SkinPoolSite {
    const char* id;
    uintptr_t address;
    std::string_view expected;
    uintptr_t immediateOffset;  // where the dword is inside the instruction
};

inline constexpr SkinPoolSite kSkinPool[] = {
    {"PV3", 0x9EF006, "BE 88 29 1F 02", 1},       {"PV4", 0x9EF056, "81 FE 78 2A 1F 02", 2},
    {"PV5", 0x9F0C7F, "BB AC 29 1F 02", 1},       {"PV6", 0x9F0CC2, "81 FB 9C 2A 1F 02", 2},
    {"PV7", 0x9F0D6E, "BB AC 29 1F 02", 1},       {"PV8", 0x9F0DC3, "81 FB 9C 2A 1F 02", 2},
    {"PV9", 0x9F0DCB, "BB D4 29 1F 02", 1},       {"PV10", 0x9F0E0A, "81 FB C4 2A 1F 02", 2},
};

}  // namespace player_sites
