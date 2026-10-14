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

// What the population layer calls "the partner" is the remote nearest to the local player (W10): the partner spawn zone and
// its box, the zone pick, the density level, the region and class gates, the AI vehicle target, the near-partner tests and
// the spawn origins each ask the game's getter, which answers the first remote. W10a lets the category fallback take the
// partner for any count >= 2, W10b takes 4 off the vehicle headroom per remote.
inline constexpr Site kPartnerView[] = {
    {"W10_9236E8", Kind::RetargetCall, Gate::Now, 0x9236E8, "E8 E3 44 0B 00", Thunk::NearestRemoteLocal, ""},
    {"W10_923FF4", Kind::RetargetCall, Gate::Now, 0x923FF4, "E8 D7 3B 0B 00", Thunk::NearestRemoteLocal, ""},
    {"W10_9242B7", Kind::RetargetCall, Gate::Now, 0x9242B7, "E8 14 39 0B 00", Thunk::NearestRemoteLocal, ""},
    {"W10_93625A", Kind::RetargetCall, Gate::Now, 0x93625A, "E8 71 19 0A 00", Thunk::NearestRemoteLocal, ""},
    {"W10_93B0C4", Kind::RetargetCall, Gate::Now, 0x93B0C4, "E8 07 CB 09 00", Thunk::NearestRemoteLocal, ""},
    {"W10_941320", Kind::RetargetCall, Gate::Now, 0x941320, "E8 AB 68 09 00", Thunk::NearestRemoteLocal, ""},
    {"W10_941456", Kind::RetargetCall, Gate::Now, 0x941456, "E8 75 67 09 00", Thunk::NearestRemoteLocal, ""},
    {"W10_941D29", Kind::RetargetCall, Gate::Now, 0x941D29, "E8 A2 5E 09 00", Thunk::NearestRemoteLocal, ""},
    {"W10_94345F", Kind::RetargetCall, Gate::Now, 0x94345F, "E8 6C 47 09 00", Thunk::NearestRemoteLocal, ""},
    {"W10_9468E0", Kind::RetargetCall, Gate::Now, 0x9468E0, "E8 EB 12 09 00", Thunk::NearestRemoteLocal, ""},
    {"W10_94F06A", Kind::RetargetCall, Gate::Now, 0x94F06A, "E8 61 8B 08 00", Thunk::NearestRemoteLocal, ""},
    {"W10_95135E", Kind::RetargetCall, Gate::Now, 0x95135E, "E8 6D 68 08 00", Thunk::NearestRemoteLocal, ""},
    {"W10_95600C", Kind::RetargetCall, Gate::Now, 0x95600C, "E8 BF 1B 08 00", Thunk::NearestRemoteLocal, ""},
    {"W10_95795A", Kind::RetargetCall, Gate::Now, 0x95795A, "E8 71 02 08 00", Thunk::NearestRemoteLocal, ""},
    {"W10a", Kind::Bytes, Gate::Now, 0x9413C9, "83 3D CC 03 16 02 02 75 45", Thunk::None, "83 3D CC 03 16 02 02 7C 45"},
    {"W10b", Kind::JumpOut, Gate::Now, 0x93C1F3, "E8 D8 B9 09 00 85 C0 8B C6 74 03 8D 46 FC 5E", Thunk::VehicleHeadroom, ""},
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


