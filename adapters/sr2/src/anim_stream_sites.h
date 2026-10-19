#pragma once
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string_view>

#include "partner_sites.h"

// Where milestone 16 W1 changes or reads the exe for the anim and asset streaming per remote (docs/SR2_NOTES.md "Anim and asset
// streaming per remote (milestone 16 W1)"). Copied from section 3 of the research spec M16_W1_ANIM.md; the bytes are what the exe has
// there. Each table is a phase, patched all or nothing, and only with 3 or more players and the milestone 9 sites in.
namespace anim_stream_sites {

using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// Phase A "anim sets per remote": the 0x7E handler gate (AS1), its four sends (AS2 to AS5), the need row masks (AM1 to AM3) and the
// leaver's releases (AL1). AS0a and AL1a check the game functions the thunks call.
inline constexpr Site kRequests[] = {
    {"AS1", Kind::PointerSwap, 0xE8BDCC, "30 3F A8 00", Thunk::StreamingSyncGate, ""},
    {"AS2", Kind::RetargetCall, 0xA84702, "E8 39 00 D9 FF", Thunk::SendStreamingSync, ""},
    {"AS3", Kind::RetargetCall, 0xA84145, "E8 F6 05 D9 FF", Thunk::SendStreamingSync, ""},
    {"AS4", Kind::RetargetCall, 0xA84261, "E8 DA 04 D9 FF", Thunk::SendStreamingSync, ""},
    {"AS5", Kind::RetargetCall, 0xA849DC, "E8 5F FD D8 FF", Thunk::SendStreamingSync, ""},
    {"AM1", Kind::CallBlock, 0xA82747, "C6 82 CF 00 00 00 01", Thunk::SetNeedBitEdx, "90 90"},
    {"AM2", Kind::CallBlock, 0xA82750, "C6 85 CF 00 00 00 01", Thunk::SetNeedBitEbp, "90 90"},
    {"AM3", Kind::CallBlock, 0xA82686, "C6 86 CF 00 00 00 00", Thunk::ClearNeedBitEsi, "90 90"},
    {"AL1", Kind::RetargetCall, 0x819943, "E8 28 A8 26 00", Thunk::LeaverReleases, ""},
    {"AS0a", Kind::Bytes, 0x814670, "85 F6 53 8B 5C 24 08 55", Thunk::None, ""},
    {"AL1a", Kind::Bytes, 0xA82600, "66 81 FF FF FF 74 7E 33", Thunk::None, ""},
};

// Phase B "anim need table": the table reset of the level loader, whose first call moves the table (the immediates of kNeedTable).
inline constexpr Site kNeedTableHook[] = {
    {"AT0", Kind::HookEntry, 0xA83400, "81 EC CC 03 00 00 A1 50", Thunk::MoveNeedTable, ""},
};

// An instruction of the exe with a dword that reaches the need table 0x2229528: its base or its end (0x2229BA8) plus a field offset.
// The first call of the table reset rewrites the dword (relocatedNeedRef).
struct NeedTableSite {
    const char* id;
    uintptr_t address;
    std::string_view expected;
    uintptr_t immediateOffset;  // where the dword is inside the instruction
};

inline constexpr NeedTableSite kNeedTable[] = {
    {"NT1", 0xA824E0, "B8 28 95 22 02", 1},       {"NT2", 0xA824F3, "3D A8 9B 22 02", 1},
    {"NT3", 0xA82507, "BE 28 95 22 02", 1},       {"NT4", 0xA8251F, "81 FE A8 9B 22 02", 2},
    {"NT5", 0xA82609, "B8 F6 95 22 02", 1},       {"NT6", 0xA82622, "3D 76 9C 22 02", 1},
    {"NT7", 0xA82630, "81 C1 28 95 22 02", 2},    {"NT8", 0xA826A9, "B8 F4 95 22 02", 1},
    {"NT9", 0xA826D7, "3D 74 9C 22 02", 1},       {"NT10", 0xA826E9, "81 C5 28 95 22 02", 2},
    {"NT11", 0xA8272E, "81 C2 28 95 22 02", 2},   {"NT12", 0xA82AE6, "B9 F4 95 22 02", 1},
    {"NT13", 0xA82AFE, "81 F9 74 9C 22 02", 2},   {"NT14", 0xA82B1A, "05 28 95 22 02", 1},
    {"NT15", 0xA83E1C, "BE F7 95 22 02", 1},      {"NT16", 0xA83E5A, "81 FE 77 9C 22 02", 2},
    {"NT17", 0xA84172, "BE F6 95 22 02", 1},      {"NT18", 0xA841AC, "81 FE 76 9C 22 02", 2},
    {"NT19", 0xA84AB3, "BB F4 95 22 02", 1},      {"NT20", 0xA84B34, "81 FB 74 9C 22 02", 2},
    {"NT21", 0xA84B3C, "BB F4 95 22 02", 1},      {"NT22", 0xA84BB5, "81 FB 74 9C 22 02", 2},
};

// The dword of the instruction that reaches the table, as the exe has it.
inline uint32_t immediateOf(const NeedTableSite& site) {
    const auto bytes = partner_sites::parseHex(site.expected);
    uint32_t value = 0;
    for (size_t i = 0; i < sizeof(value); ++i) value |= static_cast<uint32_t>(bytes[site.immediateOffset + i]) << (8 * i);
    return value;
}

// Where the game's code holds the table's base (NT1) and its end (NT2), for anim_stream_state::needTableView.
constexpr uintptr_t kNeedTableBaseImmediate = kNeedTable[0].address + kNeedTable[0].immediateOffset;
constexpr uintptr_t kNeedTableEndImmediate = kNeedTable[1].address + kNeedTable[1].immediateOffset;

// Phase C "stream requests": the four message handlers of the ledger.
inline constexpr Site kRequestLedger[] = {
    {"SR1", Kind::PointerSwap, 0xE8BCC4, "10 92 A7 00", Thunk::LedgerWorldAdd, ""},
    {"SR2", Kind::PointerSwap, 0xE8BCDC, "60 93 A7 00", Thunk::LedgerWorldRemove, ""},
    {"SR3", Kind::PointerSwap, 0xE8BCF4, "90 44 94 00", Thunk::LedgerCharacterAdd, ""},
    {"SR4", Kind::PointerSwap, 0xE8BD0C, "D0 44 94 00", Thunk::LedgerCharacterRemove, ""},
};

// Phase D "set change gates": the strm_set_change handler and the game's reset of its time gates.
inline constexpr Site kSetChange[] = {
    {"SC1", Kind::PointerSwap, 0xE8BCAC, "60 9D A7 00", Thunk::SetChangePerSender, ""},
    {"SC2", Kind::HookEntry, 0xA78F10, "55 56 33 F6 57 B9 D0 B2", Thunk::ResetSenderGates, ""},
};

// Phase E "trigger sets": the activity trigger character sets per remote. TS3r, TS3d and TS2a check the code the thunks resume at or call.
inline constexpr Site kTriggerSets[] = {
    {"TS1", Kind::PointerSwap, 0xE8ABF0, "A0 85 68 00", Thunk::TriggerSetPerSender, ""},
    {"TS2", Kind::HookEntry, 0x687B40, "83 EC 0C A1 50 0B E8 00", Thunk::TriggerTick, ""},
    {"TS3", Kind::JumpOut, 0x68875B, "83 C7 04 83 FF 08 7C 9D", Thunk::TriggerCleanupPass, ""},
    {"TS3r", Kind::Bytes, 0x688700, "8B 74 3C 08 85 F6 74 13", Thunk::None, ""},
    {"TS3d", Kind::Bytes, 0x688763, "6A 00 6A 04", Thunk::None, ""},
    {"TS4", Kind::HookEntry, 0x687A80, "83 EC 08 A1 50 0B E8 00", Thunk::TriggerReset, ""},
    {"TS2a", Kind::Bytes, 0xA79240, "81 EC 20 05 00 00 A1 50", Thunk::None, ""},
};

// Phase F "anim stream trace" (trace=1 only): the anim set load. AN1 to AN3 are in player_sites::kAnimTrace.
inline constexpr Site kTrace[] = {
    {"AN4", Kind::HookEntry, 0xA82EA0, "A0 48 8B 52 02 81 EC 1C", Thunk::TraceAnimLoad, ""},
};

// A phase: its sites, and the need table immediates that are checked with them (written by the hook's first call).
struct Phase {
    const char* name;
    std::span<const Site> sites;
    bool traceOnly;
    std::span<const NeedTableSite> immediates = {};
};

// The sites a phase writes: its code edits and hooks (not the checked rows) and the immediates of the need table.
constexpr size_t writtenCount(const Phase& phase) {
    size_t written = phase.immediates.size();
    for (const Site& site : phase.sites) written += site.kind == Kind::Bytes ? 0 : 1;
    return written;
}

inline constexpr Phase kPhases[] = {
    {"anim sets per remote", kRequests, false},
    {"anim need table", kNeedTableHook, false, kNeedTable},
    {"stream requests", kRequestLedger, false},
    {"set change gates", kSetChange, false},
    {"trigger sets", kTriggerSets, false},
    {"anim stream trace", kTrace, true},
};

}  // namespace anim_stream_sites
