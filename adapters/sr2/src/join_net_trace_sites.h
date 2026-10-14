#pragma once
#include <cstddef>
#include <cstdint>
#include <iterator>

#include "code_patch.h"
#include "partner_sites.h"

// Where the join path trace (milestone 5b, docs/SR2_NOTES.md "Join trace") hooks the exe. Copied from the research
// table m5_patch_sites.tsv; the bytes are what the exe has there.
namespace join_net_trace_sites {

// Entry: a MinHook detour of the function that starts here. DropBranch: a `jne rel32` to the end of a function that the
// join_req handler takes to drop a request without a reply; it becomes a jmp to a thunk that logs the drop.
enum class Kind : uint8_t { Entry, DropBranch };

struct Site {
    const char* id;
    Kind kind;
    uintptr_t address;
    const char* expected;
};

constexpr uintptr_t kJoinReqHandler = 0x815BC0;
constexpr uintptr_t kJoinReqMappedDrop = 0x815BEB;     // the sender already has a member
constexpr uintptr_t kJoinReqDuplicateDrop = 0x815EB5;  // a member with the sender's address exists
constexpr uintptr_t kJoinReqDropExit = 0x81611E;       // where both drops go
constexpr size_t kBranchBytes = 6;                     // 0F 85 rel32

inline constexpr Site kSites[] = {
    {"T1", Kind::Entry, 0x8CFDE0, "51 A1 18 8C 52 02 85 C0"},
    {"T2", Kind::Entry, 0x8CC1F0, "51 56 8B 74 24 0C 57 56"},
    {"T3", Kind::Entry, 0x8138F0, "51 8B 44 24 08 8A 10 80"},
    {"T4", Kind::Entry, kJoinReqHandler, "55 8B EC 83 E4 F8 81 EC"},
    {"T4a", Kind::DropBranch, kJoinReqMappedDrop, "0F 85 2D 05 00 00"},
    {"T4b", Kind::DropBranch, kJoinReqDuplicateDrop, "0F 85 63 02 00 00"},
    {"T5", Kind::Entry, 0x816F60, "81 EC 24 05 00 00 A1 50"},
    {"T6", Kind::Entry, 0x80F640, "83 EC 18 A1 50 0B E8 00"},
    {"T7", Kind::Entry, 0x7F9590, "51 56 57 BF 28 58 A1 02"},
    {"T8", Kind::Entry, 0x815930, "81 EC 28 05 00 00 A1 50"},
    {"T9", Kind::Entry, 0x8163F0, "80 3D 56 8B 52 02 00 8B"},
    {"T10", Kind::Entry, 0x8162E0, "55 8B EC 83 E4 F8 81 EC"},
};
inline constexpr size_t kSiteCount = std::size(kSites);

// Where a drop branch goes when taken.
inline uintptr_t takenTarget(const Site& site) {
    const std::vector<uint8_t> bytes = partner_sites::parseHex(site.expected);
    const uint32_t displacement = bytes[2] | bytes[3] << 8 | bytes[4] << 16 | static_cast<uint32_t>(bytes[5]) << 24;
    return site.address + kBranchBytes + displacement;
}

// The check of a site, plus for a drop branch a jmp to `thunk` over its 6 bytes (the thunk takes the branch itself).
inline CodePatch patchFor(const Site& site, uintptr_t thunk) {
    CodePatch patch{site.id, site.address, partner_sites::parseHex(site.expected), {}};
    if (site.kind == Kind::DropBranch) {
        const auto jump = relativeBranch(kJmpRel32, site.address, thunk);
        patch.replacement.assign(jump.begin(), jump.end());
        patch.replacement.resize(kBranchBytes, 0x90);
    }
    return patch;
}

}  // namespace join_net_trace_sites
