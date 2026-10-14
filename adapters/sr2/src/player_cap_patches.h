#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#include "code_patch.h"
#include "sr2_addresses.h"

// The code edits of the M5 set as data (pure, unit tested), all written together for 3 or more players:
//   the session info maximum and isCoop's member count test (docs/SR2_NOTES.md, "Player cap"),
//   the duplicate member search call that lets players behind one NAT join ("Home network"),
//   the guest branch of the member state callback that welcomes a late joiner ("Late joiners"),
//   the character instance create that clears the look tables of its pool index ("Looks"),
//   the member leave path: a guest drops the leaver's records, only the owner renames, and the entry is checked for the
//   hook that reissues ("Member leave"),
//   the entry of the spawn group create, checked for the hook that refuses a create the pools cannot fill ("Spawn budget").
namespace player_cap_patches {

constexpr uint8_t kNop = 0x90;

// The imm8 of `and ebx, imm8`: the co-op flag then yields `maxPlayers` instead of 2 once the block adds 12 back.
inline uint8_t sessionInfoMask(uint32_t maxPlayers) {
    return static_cast<uint8_t>(maxPlayers - sr2::kSessionInfoBaseMax);
}

// Where the edits go to.
struct Thunks {
    uintptr_t coopMemberReady;      // isCoop's member count test jumps here
    uintptr_t duplicateJoinSender;  // replaces the duplicate member search call of the join_req handler
    uintptr_t guestWelcome;         // the guest branch of the member state change callback jumps here
    uintptr_t resetIndexLooks;      // the character instance create of a player human goes here
    uintptr_t traceTintSend;        // trace only: the tint list of a components message goes here
    uintptr_t dropLeaverRecord;     // the leaver-owned test of the leave loop jumps here
    uintptr_t keepRename;           // the id test of the leave rename loop jumps here
};

// The edit of `expected` at `address` into a jmp rel32 to `thunk` and nops over the rest.
inline CodePatch jumpOut(const char* name, uintptr_t address, std::vector<uint8_t> expected, uintptr_t thunk) {
    const auto jump = relativeBranch(kJmpRel32, address, thunk);
    std::vector<uint8_t> written(jump.begin(), jump.end());
    written.resize(expected.size(), kNop);
    return {name, address, std::move(expected), std::move(written)};
}

// All edits together (the look send trace only with `trace`), or none when `maxPlayers` is the shipped co-op's.
inline std::vector<CodePatch> forPlayers(uint32_t maxPlayers, const Thunks& thunks, bool trace = false) {
    if (maxPlayers <= sr2::kCoopDefaultPlayers) return {};
    const std::vector<uint8_t> maxBlock = {0x83, 0xE3, 0xF6, 0x83, 0xC3, 0x0C};
    std::vector<uint8_t> widenedBlock = maxBlock;
    widenedBlock[sr2::kSessionInfoMaxMaskOffset] = sessionInfoMask(maxPlayers);

    std::vector<CodePatch> patches = {
        {"session_info_max", sr2::kSessionInfoMaxBlock, maxBlock, widenedBlock},
        jumpOut("coop_member_count_test", sr2::kCoopMemberCountTest, {0x83, 0x79, 0x30, 0x02, 0x75, 0x2D}, thunks.coopMemberReady),
        retargetedCall("duplicate_sender_call", sr2::kDuplicateSenderCall, sr2::kFindDuplicateMember,
                       thunks.duplicateJoinSender),
        jumpOut("guest_welcome_member", sr2::kGuestMemberBranch, {0x83, 0x7F, 0x3C, 0x0D, 0x75, 0x0D}, thunks.guestWelcome),
        retargetedCall("reset_index_looks", sr2::kCharInstanceCreateCall, sr2::kCharInstanceCreate, thunks.resetIndexLooks),
        jumpOut("leave_drop_leaver_record", sr2::kLeaveLeaverBranch, {0x38, 0x0E, 0x0F, 0x85, 0x91, 0x00, 0x00, 0x00},
                thunks.dropLeaverRecord),
        jumpOut("leave_keep_rename", sr2::kLeaveRenameCheck,
                {0x66, 0x8B, 0x4E, 0x02, 0x66, 0xC1, 0xE9, 0x0C, 0x66, 0x3B, 0xCB, 0x75, 0x31}, thunks.keepRename),
        {"leave_entry", sr2::kLeaveFunction, {0x83, 0xEC, 0x10, 0xA1, 0x14, 0x8C, 0x52, 0x02}, {}},
        {"spawn_budget_group_create", sr2::kGroupCreate, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8}, {}},
    };
    if (trace) patches.push_back(retargetedCall("trace_tint_send", sr2::kTintSendCall, sr2::kTintListBuild, thunks.traceTintSend));
    return patches;
}

}  // namespace player_cap_patches
