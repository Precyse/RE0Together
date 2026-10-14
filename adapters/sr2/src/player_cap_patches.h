#pragma once
#include <cstdint>
#include <vector>

#include "code_patch.h"
#include "sr2_addresses.h"

// The code edits of milestones 5 and 5d as data (pure, unit tested): the session info maximum, isCoop's member count
// test (docs/SR2_NOTES.md, "Player cap"), the duplicate member search call that lets players behind one NAT join
// ("Home network") the guest branch of the member state callback that welcomes a late joiner ("Late joiners") the character instance create that clears the look tables of its pool index and a null guard in the ambient spawner's loop ("Looks").
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
    uintptr_t skipNullElement;      // the load of an ambient spawner element and its template jumps here
};

// All edits together (the look send trace only with `trace`), or none when `maxPlayers` is the shipped co-op's.
inline std::vector<CodePatch> forPlayers(uint32_t maxPlayers, const Thunks& thunks, bool trace = false) {
    if (maxPlayers <= sr2::kCoopDefaultPlayers) return {};
    const std::vector<uint8_t> maxBlock = {0x83, 0xE3, 0xF6, 0x83, 0xC3, 0x0C};
    std::vector<uint8_t> widenedBlock = maxBlock;
    widenedBlock[sr2::kSessionInfoMaxMaskOffset] = sessionInfoMask(maxPlayers);

    const std::vector<uint8_t> countTest = {0x83, 0x79, 0x30, 0x02, 0x75, 0x2D};
    const auto jump = relativeBranch(kJmpRel32, sr2::kCoopMemberCountTest, thunks.coopMemberReady);
    std::vector<uint8_t> jumpOut(jump.begin(), jump.end());
    jumpOut.resize(countTest.size(), kNop);

    const std::vector<uint8_t> guestBranch = {0x83, 0x7F, 0x3C, 0x0D, 0x75, 0x0D};
    const auto welcome = relativeBranch(kJmpRel32, sr2::kGuestMemberBranch, thunks.guestWelcome);
    std::vector<uint8_t> welcomeJump(welcome.begin(), welcome.end());
    welcomeJump.resize(guestBranch.size(), kNop);

    const std::vector<uint8_t> elementLoad = {0x8B, 0x75, 0x00, 0x8B, 0x4E, 0x34};
    const auto skip = relativeBranch(kJmpRel32, sr2::kAmbientElementLoad, thunks.skipNullElement);
    std::vector<uint8_t> skipJump(skip.begin(), skip.end());
    skipJump.resize(elementLoad.size(), kNop);

    std::vector<CodePatch> patches = {{"session_info_max", sr2::kSessionInfoMaxBlock, maxBlock, widenedBlock},
            {"coop_member_count_test", sr2::kCoopMemberCountTest, countTest, jumpOut},
            retargetedCall("duplicate_sender_call", sr2::kDuplicateSenderCall, sr2::kFindDuplicateMember,
                           thunks.duplicateJoinSender),
            {"guest_welcome_member", sr2::kGuestMemberBranch, guestBranch, welcomeJump},
            retargetedCall("reset_index_looks", sr2::kCharInstanceCreateCall, sr2::kCharInstanceCreate, thunks.resetIndexLooks),
            {"skip_null_ambient_element", sr2::kAmbientElementLoad, elementLoad, skipJump}};
    if (trace) patches.push_back(retargetedCall("trace_tint_send", sr2::kTintSendCall, sr2::kTintListBuild, thunks.traceTintSend));
    return patches;
}

}  // namespace player_cap_patches
