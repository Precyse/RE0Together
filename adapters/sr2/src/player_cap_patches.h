#pragma once
#include <cstdint>
#include <vector>

#include "code_patch.h"
#include "sr2_addresses.h"

// The two code edits of milestone 5 as data (pure, unit tested): the session info maximum and isCoop's member count
// test (docs/SR2_NOTES.md, "Player cap").
namespace player_cap_patches {

constexpr uint8_t kNop = 0x90;

// The imm8 of `and ebx, imm8`: the co-op flag then yields `maxPlayers` instead of 2 once the block adds 12 back.
inline uint8_t sessionInfoMask(uint32_t maxPlayers) {
    return static_cast<uint8_t>(maxPlayers - sr2::kSessionInfoBaseMax);
}

// Both edits, or none when `maxPlayers` is the shipped co-op's. `thunk` is where isCoop's member count test jumps to.
inline std::vector<CodePatch> forPlayers(uint32_t maxPlayers, uintptr_t thunk) {
    if (maxPlayers <= sr2::kCoopDefaultPlayers) return {};
    const std::vector<uint8_t> maxBlock = {0x83, 0xE3, 0xF6, 0x83, 0xC3, 0x0C};
    std::vector<uint8_t> widenedBlock = maxBlock;
    widenedBlock[sr2::kSessionInfoMaxMaskOffset] = sessionInfoMask(maxPlayers);

    const std::vector<uint8_t> countTest = {0x83, 0x79, 0x30, 0x02, 0x75, 0x2D};
    const auto jump = relativeBranch(kJmpRel32, sr2::kCoopMemberCountTest, thunk);
    std::vector<uint8_t> jumpOut(jump.begin(), jump.end());
    jumpOut.resize(countTest.size(), kNop);

    return {{"session_info_max", sr2::kSessionInfoMaxBlock, maxBlock, widenedBlock},
            {"coop_member_count_test", sr2::kCoopMemberCountTest, countTest, jumpOut}};
}

}  // namespace player_cap_patches
