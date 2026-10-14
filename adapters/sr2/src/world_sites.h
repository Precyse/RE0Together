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

// Each member gets the upload rate it reported (W9), not that rate divided by the number of remotes.
inline constexpr Site kUpload[] = {
    {"W9a", Kind::Bytes, Gate::Now, 0x814267, "F7 74 24 18", Thunk::None, "90 90 90 90"},
};

// A member's creation queue never overflows (W8): the three appends and the drain.
inline constexpr Site kQueue[] = {
    {"W8a", Kind::JumpOut, Gate::Now, 0x80517F, "8B 88 F0 21 00 00 89 BC 88 F0 1D 00 00 8A 88 B0 01 00 00 83 80 F0 21 00 00 01", Thunk::QueueAppendCreate, ""},
    {"W8b", Kind::JumpOut, Gate::Now, 0x805248, "8B 8A F0 21 00 00 8B 58 04 89 9C 8A F0 1D 00 00 01 AA F0 21 00 00", Thunk::QueueAppendBatch, ""},
    {"W8c", Kind::HookEntry, Gate::Now, 0x804940, "8B 88 F0 21 00", Thunk::QueueAppendFunction, ""},
    {"W8d", Kind::HookEntry, Gate::Now, 0x807840, "81 EC 24 05 00 00", Thunk::QueueDrain, ""},
};

}  // namespace world_sites


