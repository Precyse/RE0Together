// The replication site tables and the patches built from them (no game).
#include <cstdio>
#include <set>

#include "../src/replication_patches.h"
#include "check.h"

namespace {

template <typename Sites, typename Key>
bool allDistinct(const Sites& sites, Key key) {
    std::set<uintptr_t> seen;
    for (const auto& site : sites) {
        if (!seen.insert(key(site)).second) return false;
    }
    return true;
}

bool entriesAreConsistent() {
    for (const auto& site : replication_sites::kEntries) {
        const bool sameShape = site.n1Bytes[0] == site.n8Bytes[0] && site.n1Bytes[1] == site.n8Bytes[1];
        if (!sameShape || site.n1 == site.n8) return false;
    }
    return true;
}

bool branchesAreShortJe() {
    for (const CodePatch& patch : replication_patches::branchPatches(kJeShort, kJmpShort)) {
        if (patch.expected.size() != 2 || patch.expected[0] != 0x74 || patch.expected[1] == 0 ||
            patch.replacement.size() != 1 || patch.replacement[0] != 0xEB) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    check(replication_sites::kEntryCount == 24, "24 entry jumps (8 bundles x alloc, free, free_all)");
    check(replication_sites::kBranchCount == 32, "32 peer index branches");
    check(replication_sites::kAllocatorCount == 8, "8 N=8 allocator objects");
    check(allDistinct(replication_sites::kEntries, [](const auto& s) { return s.n1; }), "no duplicate N=1 entry");
    check(allDistinct(replication_sites::kEntries, [](const auto& s) { return s.n8; }), "no duplicate N=8 twin");
    check(allDistinct(replication_sites::kBranches, [](const auto& s) { return s.address; }), "no duplicate branch");
    check(allDistinct(replication_sites::kN8Allocators, [](const auto& s) { return s.address; }),
          "no duplicate allocator");
    check(entriesAreConsistent(), "each twin starts like its N=1 function and differs from it");
    check(branchesAreShortJe(), "every branch is a short je (0x74, displacement) that becomes jmp (0xEB)");

    const auto patches = replication_patches::replicationPatches();
    check(patches.size() == 32 + 2 * 24 + 2, "patch list: 32 branches, 24 entries with their twins, 2 set names");
    size_t writes = 0;
    bool lengthsFit = true;
    for (const CodePatch& patch : patches) {
        writes += !patch.replacement.empty();
        lengthsFit = lengthsFit && patch.replacement.size() <= patch.expected.size();
    }
    check(writes == 32 + 24 + 2, "58 sites are written, the 24 twins only checked");
    check(lengthsFit, "no replacement is longer than the bytes it was checked against");

    const auto back = replication_patches::branchPatches(kJmpShort, kJeShort);
    check(back.size() == 32 && back[0].expected[0] == 0xEB && back[0].replacement[0] == 0x74, "branches revert to je");

    const auto jump = relativeBranch(kJmpRel32, 0x846B20, 0x845F80);
    check(jump[0] == 0xE9 && jump[1] == 0x5B && jump[2] == 0xF4 && jump[3] == 0xFF && jump[4] == 0xFF,
          "jmp rel32 backwards (0x846B20 -> 0x845F80)");
    const auto call = relativeBranch(kCallRel32, 0x7F7220, 0x7F6CE0);
    check(call[0] == 0xE8 && call[1] == 0xBB && call[2] == 0xFA && call[3] == 0xFF && call[4] == 0xFF,
          "call rel32 reproduces the shipped activation call bytes");

    std::vector<CodePatch> names;
    replication_patches::addSetNamePatches(names);
    check(names.size() == 2 && names[0].address == 0x7F71D6 && names[1].address == 0x7F7084 &&
              names[0].expected == std::vector<uint8_t>{0xB0, 0x62, 0xE2, 0x00} &&
              names[0].replacement == std::vector<uint8_t>{0xB8, 0x62, 0xE2, 0x00},
          "set names: imm32 at 0x7F71D6 and 0x7F7084, 0xE262B0 -> 0xE262B8");

    check(replication_sites::isSessionSlot(0, 4) && replication_sites::isSessionSlot(3, 4), "slots 0..3 for 4 players");
    check(!replication_sites::isSessionSlot(4, 4) && !replication_sites::isSessionSlot(0xFF, 8),
          "slot 4 of 4 and the no-slot 0xFF are refused");
    return g_failures == 0 ? 0 : 1;
}
