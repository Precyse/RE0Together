#pragma once
#include <iterator>

#include "partner_sites.h"

// Where milestone 11 changes or reads the exe for the players area (docs/SR2_NOTES.md, "Fast fire"). Copied from the
// research table of M11_SHATTERED_IDEAS.md; the bytes are what the exe has there. Each table is a phase, patched all or
// nothing, and only with 3 or more players.
namespace player_sites {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// A guest's net clock never steps back and only takes the host's clock (CK1, CK2).
inline constexpr Site kNetClock[] = {
    {"CK1", Kind::HookEntry, Gate::Now, 0x7FCBF0, "8B 44 24 08 83 78 04 00", Thunk::NetClockMessage, ""},
    {"CK2", Kind::HookEntry, Gate::Now, 0x7FC930, "83 EC 14 80 3D 56 8B 52 02 00", Thunk::NetClockTick, ""},
};

// trace=1 only: which sender a player bundle packet came from, against the owner of the object it updates (BUT).
inline constexpr Site kBundleSender[] = {
    {"BUT", Kind::PointerSwap, Gate::Now, 0xE2B454, "D0 69 86 00", Thunk::TraceBundleSender, ""},
};

}  // namespace player_sites
