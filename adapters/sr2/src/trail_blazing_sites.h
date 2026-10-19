#pragma once
#include <span>

#include "partner_sites.h"

// Where milestone 17 wave 3 changes the exe for Trail Blazing with 3 or 4 players (docs/SR2_NOTES.md, "Trail Blazing (milestone 17
// wave 3)"). Copied from the research table of M17_W3_TRAIL.md section 3; the bytes are what the exe has there. Each table is a set,
// written all or nothing and only with the milestone 7 activity logic installed. A `Bytes` row is a check row: it guards the byte
// where a jump out resumes.
namespace trail_blazing_sites {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// Set on fire at the level start, put out at the cleanup and the end, the water fail: a remote is never set on fire without also
// being put out.
inline constexpr Site kBurn[] = {
    {"TB2", Kind::JumpOut, Gate::Now, 0x58CDD5, "85 F6 74 1C 6A 00", Thunk::TrailLevelStartEachRemote, ""},
    {"TB2r", Kind::Bytes, Gate::Now, 0x58CDF5, "8B D7 E8 94 6C 43 00", Thunk::None, ""},
    {"TB3", Kind::JumpOut, Gate::Now, 0x58CE16, "85 F6 74 32 8B D6", Thunk::TrailLevelStepEachRemote, ""},
    {"TB3r", Kind::Bytes, Gate::Now, 0x58CE4C, "5F 5E 5D 88 1D 23 96 52 02 5B C2 08 00", Thunk::None, ""},
    {"TB4", Kind::RetargetCall, Gate::Now, 0x58D88D, "E8 1E F6 3F 00", Thunk::TrailPutOutEachRemote, ""},
    {"TB5", Kind::RetargetCall, Gate::Now, 0x58D97A, "E8 31 F5 3F 00", Thunk::TrailPutOutEachRemote, ""},
    {"TB8", Kind::JumpOut, Gate::Now, 0x58DDB7, "83 7D 28 02 75 37", Thunk::TrailWaterFailEachRemote, ""},
    {"TB8r", Kind::Bytes, Gate::Now, 0x58DDF4, "8B 4D 24 85 C9", Thunk::None, ""},
};

// The fire effect, the fire nodes and the flammable objects.
// TB7 replaces `test bl, bl; je; mov edi, [esp+0x18]; call 0x58D9B0`; the tail `jmp +6` lands on 0x58DC80, past the dead bytes.
inline constexpr Site kFire[] = {
    {"TB1", Kind::RetargetCall, Gate::Now, 0x55817E, "E8 4D FA 47 00", Thunk::TrailNearestToFire, ""},
    {"TB6", Kind::JumpOut, Gate::Now, 0x58DA83, "83 7C 24 1C 00 74 50", Thunk::TrailFireNodesEachRemote, ""},
    {"TB6r", Kind::Bytes, Gate::Now, 0x58DADA, "8B 44 24 14 83 C0 01", Thunk::None, ""},
    {"TB7", Kind::CallBlock, Gate::Now, 0x58DC73, "84 DB 74 09 8B 7C 24 18 E8 30 FD FF FF", Thunk::TrailObjectsEachRemote, "EB 06"},
};

// The stage start: the grid and the request to leave a vehicle.
inline constexpr Site kStart[] = {
    {"TB9", Kind::JumpOut, Gate::Now, 0x58EE94, "74 1B 6A 00 6A 00", Thunk::TrailStartPlaceEachRemote, ""},
    {"TB9r", Kind::Bytes, Gate::Now, 0x58EEB1, "8B 53 04 6A 00", Thunk::None, ""},
    {"TB10", Kind::JumpOut, Gate::Now, 0x58FA8E, "85 ED 74 6D 8B 45 44", Thunk::TrailStartExitEachRemote, ""},
    {"TB10r", Kind::Bytes, Gate::Now, 0x58FAFF, "85 DB 74 16 8B C3", Thunk::None, ""},
};

// The checkpoint: the distance of a riding remote.
inline constexpr Site kCheckpoint[] = {
    {"TB11", Kind::JumpOut, Gate::Now, 0x590386, "8D 48 14 8B 44 24 14", Thunk::TrailCheckpointDistance, ""},
    {"TB11r", Kind::Bytes, Gate::Now, 0x5903B1, "8B 46 44 85 C0", Thunk::None, ""},
};

struct Set {
    const char* name;
    std::span<const Site> sites;
};

inline constexpr Set kSets[] = {
    {"burn", kBurn},
    {"fire", kFire},
    {"start", kStart},
    {"checkpoint", kCheckpoint},
};

}  // namespace trail_blazing_sites
