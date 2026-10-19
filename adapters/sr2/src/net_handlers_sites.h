#pragma once
#include <span>

#include "partner_sites.h"

// Where milestone 16 wave 2 changes the exe (docs/SR2_NOTES.md, "Net handlers (M16 wave 2)"): the net handlers that kept one
// remote's state keep each remote's, and the notices and the session lost dialog name the right player. The busted wait (misc_19) is
// revive_sites.h and the replay answers are mission_flow_sites.h. The bytes are what the exe
// has there. Each table is one set, written all or nothing and only with 3 or more players and the milestone 9 logic. A row of kind
// Bytes with an empty tail (an "r" or "c" row) is only checked: the resume point or called function a thunk relies on.
namespace net_handlers_sites {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// scrpt_02 and scrpt_03 (0xA25D10, 0xA25D50): the race resource set is held by a bit per remote slot. RHc is the release the leave
// rule calls.
inline constexpr Site kRaceHold[] = {
    {"RH1", Kind::JumpOut, Gate::Now, 0xA25D1A, "80 3D 1E 98 52 02 00 75 28 80 3D 1D 98 52 02 00 C6 05 1E 98 52 02 01 75 18", Thunk::RaceHoldAcquire, ""},
    {"RH1r1", Kind::Bytes, Gate::Now, 0xA25D33, "68 18 C8 40 01 E8 93 E7 FF FF", Thunk::None, ""},
    {"RH1r2", Kind::Bytes, Gate::Now, 0xA25D4B, "C3", Thunk::None, ""},
    {"RH2", Kind::JumpOut, Gate::Now, 0xA25D61, "80 3D 1E 98 52 02 00 74 27 80 3D 1D 98 52 02 00 C6 05 1E 98 52 02 00 75 17", Thunk::RaceHoldRelease, ""},
    {"RH2r1", Kind::Bytes, Gate::Now, 0xA25D7A, "BE 18 C8 40 01 E8 DC F9 FF FF", Thunk::None, ""},
    {"RH2r2", Kind::Bytes, Gate::Now, 0xA25D91, "5E 8B E5 5D C3", Thunk::None, ""},
    {"RHc", Kind::Bytes, Gate::Now, 0xA25760, "55 8B EC 83 E4 F8 83 EC", Thunk::None, ""},
};

// misc_1c (0x5440C0), the start handshake of the mission replay: a guest takes it only from the host. The answers, the prompt and the
// wait dialog's names of the replay are mission_flow_sites.h (MF12 to MF17).
inline constexpr Site kReplayStart[] = {
    {"RP4", Kind::HookEntry, Gate::Now, 0x5440C0, "A1 14 8C 52 02 83 EC 08", Thunk::ReplayStart, ""},
};

// The confessional's status (0x7A8E70) and the text names (0x7A8C60): the remote that is farthest and the one with the least cash
// stand for "the partner". CFc is the price conversion the confessional notices call.
inline constexpr Site kConfessRule[] = {
    {"CF1", Kind::RetargetCall, Gate::Now, 0x7A8E95, "E8 36 ED 22 00", Thunk::ConfessFarthest, ""},
    {"CF2", Kind::RetargetCall, Gate::Now, 0x7A8F48, "E8 83 EC 22 00", Thunk::ConfessPoorestAtPrice, ""},
    {"CF3", Kind::RetargetCall, Gate::Now, 0x7A8CCF, "E8 FC EE 22 00", Thunk::ConfessFarthest, ""},
    {"CF4", Kind::RetargetCall, Gate::Now, 0x7A8D71, "E8 5A EE 22 00", Thunk::ConfessPoorest, ""},
    {"CFc", Kind::Bytes, Gate::Now, 0x4B2680, "D9 44 24 04 DC 0D 90 C1", Thunk::None, ""},
};

// misc_00: the host's notice goes to the remotes that fail the test (CF5), and a receiver of a guest's notice shows it only when it
// fails the test itself (CF8, CF9). CFr is the handler's epilogue the resume points use.
inline constexpr Site kConfessNotices[] = {
    {"CF5", Kind::RetargetCall, Gate::Now, 0x7A9153, "E8 E8 B5 06 00", Thunk::ConfessNotice, ""},
    {"CF8", Kind::JumpOut, Gate::Now, 0x7A94F2, "80 FB 02 75 18", Thunk::ConfessFarNotice, ""},
    {"CF8r", Kind::Bytes, Gate::Now, 0x7A94F7, "D9 44 24 0C 51 B0 03 D9", Thunk::None, ""},
    {"CF9", Kind::JumpOut, Gate::Now, 0x7A950F, "80 FB 06 74 59", Thunk::ConfessPoorNotice, ""},
    {"CF9r1", Kind::Bytes, Gate::Now, 0x7A956D, "D9 44 24 0C 51 B0 05 D9", Thunk::None, ""},
    {"CF9r2", Kind::Bytes, Gate::Now, 0x7A9514, "80 FB 04 74 09", Thunk::None, ""},
    {"CFr", Kind::Bytes, Gate::Now, 0x7A95D7, "5E 5B 8B E5 5D C3", Thunk::None, ""},
};

// misc_00: the lock [0x252877C] ("a remote is in a confessional") becomes a set of holders, set by sub 0 (CF6) and cleared by sub 3 (CF7).
inline constexpr Site kConfessLock[] = {
    {"CF6", Kind::CallBlock, Gate::Now, 0x7A94E4, "A3 7C 87 52 02", Thunk::ConfessLockSet, ""},
    {"CF7", Kind::CallBlock, Gate::Now, 0x7A95C5, "C7 05 7C 87 52 02 00 00 00 00", Thunk::ConfessLockClear, "90 90 90 90 90"},
};

// The session lost dialog (0x8CBA50) names the host's human on a guest.
inline constexpr Site kSessionLost[] = {
    {"SL1", Kind::RetargetCall, Gate::Now, 0x8CBACD, "E8 FE C0 10 00", Thunk::SessionLostHuman, ""},
};

struct Set {
    const char* name;
    std::span<const Site> sites;
};

inline constexpr Set kSets[] = {
    {"race hold", kRaceHold},
    {"replay start", kReplayStart},
    {"confessional rule", kConfessRule},
    {"confessional notices", kConfessNotices},
    {"confessional lock", kConfessLock},
    {"session lost name", kSessionLost},
};

}  // namespace net_handlers_sites
