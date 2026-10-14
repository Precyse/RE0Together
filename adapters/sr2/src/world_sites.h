#pragma once
#include <iterator>

#include "partner_sites.h"

// Where milestone 9 changes the exe for the world: the ambient population, streaming and the object hand-over
// (docs/SR2_NOTES.md, "Population" and the sections after it). Copied from the research table m9_world_sites.tsv; the bytes
// are what the exe has there (a flipped branch is checked together with the compare before it). Each table is a phase,
// patched all or nothing, and only with 3 or more players.
namespace world_sites {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// The budgets of the ambient population follow the number of separate player areas (W1). The two flipped branches turn
// the game's `[0x21603CC] == 2` (humans in the game) into `>= 2`.
inline constexpr Site kBudget[] = {
    {"W1a", Kind::HookEntry, Gate::Now, 0x93E8D0, "83 EC 24 53 55 8B 6C 24 34", Thunk::AreaBudget, ""},
    {"W1b", Kind::Bytes, Gate::Now, 0x9292B8, "39 0D CC 03 16 02 75", Thunk::None, "39 0D CC 03 16 02 7C"},
    {"W1c", Kind::Bytes, Gate::Now, 0x90D176, "83 3D CC 03 16 02 02 75", Thunk::None, "83 3D CC 03 16 02 02 7C"},
};

// The lowest slot of a cluster of players spawns for it (W2).
inline constexpr Site kCluster[] = {
    {"W2a", Kind::HookEntry, Gate::Now, 0x957A40, "A0 48 8B 52 02 83 EC 08", Thunk::ClusterLeaderFar, ""},
};

}  // namespace world_sites

