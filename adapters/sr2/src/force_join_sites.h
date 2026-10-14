#pragma once
#include <cstddef>
#include <cstdint>
#include <iterator>

// Where milestone 6 (force join, docs/SR2_NOTES.md "Force join") touches the exe. The code rows are copied from the
// research table m6_patch_sites.tsv (the bytes are what the exe has there); the rest are game globals and fields.
namespace force_join_sites {

// A game function: the adapter detours F1 and calls the others, so its first bytes are only checked.
struct Site {
    const char* id;
    uintptr_t address;
    const char* expected;
};

constexpr uintptr_t kFrontEndTick = 0x73DCB0;     // F1: the state 2 tick of the game state dispatcher, cdecl, no arguments
constexpr uintptr_t kStartMatchmaking = 0x827620; // G1: the co-op query, cdecl, no arguments
constexpr uintptr_t kStopMatchmaking = 0x827540;  // G2: stops the query, cdecl, no arguments
constexpr uintptr_t kJoinByIndex = 0x73E5A0;      // G3: EAX = index among the listed entries
constexpr uintptr_t kServerString = 0x8E4960;     // G4: EAX = server, ECX = key, [esp+4] = default; returns the text
constexpr uintptr_t kLogin = 0x80A150;            // login_to_online_network's body, cdecl, no arguments

inline constexpr Site kSites[] = {
    {"F1", kFrontEndTick, "81 EC D0 01 00 00 A1 50 0B E8 00"},
    {"G1", kStartMatchmaking, "55 8B EC 83 E4 F8 83 EC 20"},
    {"G2", kStopMatchmaking, "51 53 56 E8 B8 E9 00 00"},
    {"G3", kJoinByIndex, "B1 01 53 32 DB 85 C0 A3 9C 7E 52 02"},
    {"G4", kServerString, "83 EC 0C 85 C0 57 75 05"},
    {"G12", kLogin, "8B 0D D0 8B 52 02 85 C9"},
};
inline constexpr size_t kSiteCount = std::size(kSites);

// Game text buffers (char[0x40] each).
constexpr size_t kTextBytes = 0x40;
constexpr uintptr_t kHostSessionName = 0x212AA08;  // H1: the QR2 key online_name
constexpr uintptr_t kCoopPassword = 0x212AA88;     // H2: what join_req compares; the query clears it
constexpr uintptr_t kBrowserNameFilter = 0x212A9C8;  // G6: the browser's online_name LIKE filter
constexpr uintptr_t kJoinPassword = 0x212AA48;     // G8: what join_req carries

// Game integers.
constexpr uintptr_t kJoinType = 0xE97310;          // H3: 0 public, 1 friends, 2 private
constexpr uintptr_t kCustomGameFilter = 0x212A980; // G7
constexpr uintptr_t kNameToUse = 0x2528BC4;        // G10
constexpr uintptr_t kRankedJoinByte = 0x2528D91;   // G11: byte
constexpr uint32_t kJoinTypePublic = 0;
constexpr uint32_t kCustomGameAny = 0;
constexpr uint32_t kNameToUseOnline = 1;

// What the guest reads before it acts.
constexpr uintptr_t kOnlineState = 0x2529334;      // S1
constexpr uint32_t kOnlineStateConnected = 4;
constexpr uintptr_t kModuleRing = 0x252933C;       // S2: online modules, next at +4, type at +0xC
constexpr uintptr_t kStartRequest = 0x144E75C;     // S4: a pending game state request
constexpr uintptr_t kPendingGame = 0xEB2230;       // S5: byte, a join or host start is pending
constexpr uintptr_t kCoopDialogOpen = 0x2528D20;   // S6: the load or create dialog

// Online module (the ring) and the browser's entries.
constexpr uintptr_t kModuleNextOffset = 4;
constexpr uintptr_t kModuleTypeOffset = 0xC;
constexpr uint32_t kModuleTypeBrowser = 4;
constexpr uintptr_t kBrowserStateOffset = 0x10;    // 0 idle, 1 querying, 2 done, 3 error
constexpr uintptr_t kBrowserEntriesOffset = 0x3C;  // the first entry of a circular list
constexpr uintptr_t kEntryListedOffset = 0x65;     // byte: counted by the join index
constexpr uintptr_t kEntryServerOffset = 0x94;     // the SBServer
constexpr uintptr_t kEntryNextOffset = 0x98;
constexpr uintptr_t kEntryPasswordFlagOffset = 0x8E;  // byte: the join asks for a password
constexpr uintptr_t kOnlineNameKey = 0xE33268;     // "online_name"

// Every walk of the module ring or the entry list stops here.
constexpr size_t kMaxModulesWalked = 16;
constexpr size_t kMaxEntriesWalked = 256;

}  // namespace force_join_sites
