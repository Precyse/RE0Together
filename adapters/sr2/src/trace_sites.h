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

// The pool census (milestone 11 B6, docs/SR2_NOTES.md, "Pool census"): the NPC human setup, which counts a create that
// returned nothing.
inline constexpr Site kCensus[] = {
    {"NC1", Kind::HookEntry, Gate::Now, 0x9D0DE0, "51 53 8A 5C 24 0C 84 DB", Thunk::TraceNpcSetup, ""},
};

}  // namespace trace_sites
