#pragma once
#include <vector>

#include "code_patch.h"
#include "replication_sites.h"
#include "sr2_addresses.h"

// The code edits that switch co-op from the 1-peer to the 8-peer deltacomp code, as one list so that they are
// verified and applied together.
namespace replication_patches {

inline std::vector<uint8_t> littleEndian(uint32_t value) {
    return {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value >> 16),
            static_cast<uint8_t>(value >> 24)};
}

// N=1 entry -> jmp to the N=8 twin; the twin's first bytes are checked too.
inline void addEntryPatches(std::vector<CodePatch>& out) {
    for (const replication_sites::EntrySite& site : replication_sites::kEntries) {
        const auto jump = relativeBranch(kJmpRel32, site.n1, site.n8);
        out.push_back({site.name, site.n1, {site.n1Bytes.begin(), site.n1Bytes.end()}, {jump.begin(), jump.end()}});
        out.push_back({site.name, site.n8, {site.n8Bytes.begin(), site.n8Bytes.end()}, {}});
    }
}

// Every peer-index branch from `from` to `to` (je <-> jmp), keeping the displacement.
inline std::vector<CodePatch> branchPatches(uint8_t from, uint8_t to) {
    std::vector<CodePatch> patches;
    for (const replication_sites::BranchSite& site : replication_sites::kBranches) {
        patches.push_back({"peer index branch", site.address, {from, site.displacement}, {to}});
    }
    return patches;
}

// "coop-mp" -> "deltacomp_sa" in the two loads that name the co-op allocator set.
inline void addSetNamePatches(std::vector<CodePatch>& out) {
    for (const uintptr_t load : {sr2::kLoadCoopSetNameActivate, sr2::kLoadCoopSetNameDeactivate}) {
        out.push_back({"co-op set name", load + sr2::kCoopSetNameImmediateOffset, littleEndian(sr2::kSetNameCoopMp),
                       littleEndian(sr2::kSetNameDeltacompSa)});
    }
}

inline std::vector<CodePatch> replicationPatches() {
    std::vector<CodePatch> patches = branchPatches(replication_sites::kJeShort, replication_sites::kJmpShort);
    addEntryPatches(patches);
    addSetNamePatches(patches);
    return patches;
}

}  // namespace replication_patches
