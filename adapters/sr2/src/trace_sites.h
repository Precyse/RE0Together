#pragma once
#include <iterator>

#include "partner_sites.h"

// Where milestone 9 reads the exe for the traces of the players area (trace=1 only; docs/SR2_NOTES.md, "Player traces").
// Copied from the research table m9_players_sites.tsv.
namespace trace_sites {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

inline constexpr Site kPlayers[] = {
    {"TF1", Kind::HookEntry, Gate::Now, 0x9C1B90, "55 8B EC 83 E4 F8 81 EC", Thunk::TraceShot, ""},
    {"TF2", Kind::PointerSwap, Gate::Now, 0xE2B444, "D0 33 86 00", Thunk::TracePlayerPlayback, ""},
    {"TL2", Kind::HookEntry, Gate::Now, 0x9E9EB0, "51 A1 38 C4 E8 00", Thunk::TraceCoopBuffers, ""},
};

// The pool census (milestone 11 B6, milestone 12; docs/SR2_NOTES.md, "Pool census"): the NPC human setup, which counts a
// create that returned nothing (NC1), the chunk node loader, which takes groups and elements outside the group create (NC2),
// and the vehicle class create, whose failed attempts are named per net id (VT1).
inline constexpr Site kCensus[] = {
    {"NC1", Kind::HookEntry, Gate::Now, 0x9D0DE0, "51 53 8A 5C 24 0C 84 DB", Thunk::TraceNpcSetup, ""},
    {"NC2", Kind::HookEntry, Gate::Now, 0x9140C0, "81 EC 4C 02 00 00 A1 50 0B E8", Thunk::TraceChunkNodes, ""},
    {"VT1", Kind::PointerSwap, Gate::Now, 0xE2BA78, "30 3F 87 00", Thunk::TraceRemoteVehicle, ""},
};

}  // namespace trace_sites
