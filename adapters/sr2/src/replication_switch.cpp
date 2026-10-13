#include "replication_switch.h"

#include <atomic>

#include "code_patch.h"
#include "log.h"
#include "memory.h"
#include "replication_patches.h"
#include "replication_sites.h"
#include "sr2_addresses.h"

namespace {

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_fellBack{false};

// An idle N=8 allocator of the Strong Arm set that still takes its pool from its own pointer.
bool allocatorsLookRight() {
    bool ok = true;
    for (const replication_sites::AllocatorObject& allocator : replication_sites::kN8Allocators) {
        const uintptr_t object = allocator.address;
        const bool right = dwordField(object, sr2::kAllocatorNameOffset) == sr2::kSetNameDeltacompSa &&
                           dwordField(object, sr2::kAllocatorPoolSourceOffset) == 0 &&
                           dwordField(object, sr2::kAllocatorBlockOffset) == 0;
        if (!right) {
            logger::write("replication: %s N=8 allocator 0x%X does not match (set name 0x%X, pool source 0x%X, block 0x%X)",
                          allocator.bundle, static_cast<unsigned>(object), dwordField(object, sr2::kAllocatorNameOffset),
                          dwordField(object, sr2::kAllocatorPoolSourceOffset),
                          dwordField(object, sr2::kAllocatorBlockOffset));
            ok = false;
        }
    }
    return ok;
}

}  // namespace

namespace replication_switch {

bool sitesMatch() {
    const bool ok = codeMatches(replication_patches::replicationPatches());
    if (ok) {
        logger::write("replication: %zu entries, %zu branches and 2 set names match", replication_sites::kEntryCount,
                      replication_sites::kBranchCount);
    }
    return ok;
}

void enable() {
    if (g_enabled) return;
    if (!allocatorsLookRight() || !applyCodePatches(replication_patches::replicationPatches())) {
        logger::write("replication: not enabled, co-op keeps the 1-peer code");
        return;
    }
    for (const replication_sites::AllocatorObject& allocator : replication_sites::kN8Allocators) {
        dwordField(allocator.address, sr2::kAllocatorPoolSourceOffset) = static_cast<uint32_t>(sr2::kCurrentDeltacompPool);
    }
    g_enabled = true;
    logger::write("replication: %zu entries, %zu branches, 2 set names patched; %zu N=8 allocators follow the co-op pool",
                  replication_sites::kEntryCount, replication_sites::kBranchCount, replication_sites::kAllocatorCount);
}

void fallBackToSinglePeer() {
    if (!g_enabled || g_fellBack.exchange(true)) return;
    const bool ok = applyCodePatches(
        replication_patches::branchPatches(replication_sites::kJmpShort, replication_sites::kJeShort));
    logger::write(ok ? "replication: peer index back to 0, co-op serves one peer" : "replication: peer index could not be reset");
}

}  // namespace replication_switch
