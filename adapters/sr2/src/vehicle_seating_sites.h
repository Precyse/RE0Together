#pragma once
#include "partner_sites.h"

// Where milestone 16 wave 2 changes the exe for vehicle seating with 3 or 4 players (docs/SR2_NOTES.md, "Vehicle seating"). Copied
// from the research table of M16_W2_SEATING.md; the bytes are what the exe has there. Each table is a set, written all or nothing
// and only with 3 or more players; the checkpoint set waits for probe CP like MC4 does.
namespace vehicle_seating_sites {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// SE1: in Drug Trafficking the car of any remote counts as a partner's car, and each guest gets its own passenger seat.
// The tail is `test al, al; jnz 0x9D8769; nop x7`: the 7 nops run on into the "not a partner's car" path at 0x9D8761.
inline constexpr Site kTrafficking[] = {
    {"SE1", Kind::CallBlock, Gate::Now, 0x9D8751, "E8 7A F4 FF FF 8B 56 44 3B 90 74 0D 00 00 74 08", Thunk::TraffickingRemoteSeat,
     "84 C0 75 0F 90 90 90 90 90 90 90"},
};

// SE2: a checkpoint restore puts the player into the car of the remote that MC4 recorded at save time.
inline constexpr Site kCheckpoint[] = {
    {"SE2", Kind::RetargetCall, Gate::ProbeCp, 0x69FB63, "E8 68 80 33 00", Thunk::CheckpointVehicleHolder, ""},
};

// SE3 and SE4: the mission exit stops the car of every remote and releases what every remote holds. SE3 resumes at
// sr2::kMissionExitPartnerDone.
inline constexpr Site kMissionExit[] = {
    {"SE3", Kind::JumpOut, Gate::Now, 0x6A4E48, "E8 83 2D 33 00 8B F0 85 F6 74 32", Thunk::MissionExitStopRemotes, ""},
    {"SE4", Kind::RetargetCall, Gate::Now, 0x6A4E8E, "E8 0D 8F FF FF", Thunk::MissionExitReleaseRemotes, ""},
};

}  // namespace vehicle_seating_sites
