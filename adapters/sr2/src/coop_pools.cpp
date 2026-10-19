#include "coop_pools.h"

#include <windows.h>

#include <cstring>

#include "log.h"
#include "memory.h"
#include "pool_size.h"
#include "sr2_addresses.h"

namespace {

struct PoolSpec {
    const char* name;
    uintptr_t address;
    uint32_t (*newSize)(uint32_t originalSize, uint32_t maxPlayers, bool vehiclePool);
};

uint32_t scaledPlayerPool(uint32_t originalSize, uint32_t maxPlayers, bool) { return scaledPoolSize(originalSize, maxPlayers); }

constexpr PoolSpec kPools[] = {
    {"coop_player cpu", sr2::kCoopPlayerCpuPool, scaledPlayerPool},
    {"coop_player gpu", sr2::kCoopPlayerGpuPool, scaledPlayerPool},
    {"coop_allocators", sr2::kCoopAllocatorsPool, coopAllocatorsPoolSize},
};
constexpr size_t kPoolCount = sizeof(kPools) / sizeof(kPools[0]);

bool g_resized = false;

// The pool carries the expected name and one consistent size, and holds nothing yet: moving a pool with live
// allocations would orphan them.
bool looksLikePool(const PoolSpec& spec) {
    const char* name = reinterpret_cast<const char*>(spec.address + sr2::kPoolNameOffset);
    const uint32_t size = dwordField(spec.address, sr2::kPoolSizeOffset);
    const uint32_t used = dwordField(spec.address, sr2::kPoolUsedOffset);
    const bool nameMatches = strncmp(name, spec.name, sr2::kPoolNameLength) == 0;
    const bool sizesAgree = size != 0 && size == dwordField(spec.address, sr2::kPoolLimitOffset) &&
                            size == dwordField(spec.address, sr2::kPoolCapacityOffset);
    if (!nameMatches || !sizesAgree || used != 0) {
        logger::write("pools: %s not resized, object at 0x%X does not match (name \"%.32s\", size %u, used %u)",
                      spec.name, static_cast<unsigned>(spec.address), name, size, used);
    }
    return nameMatches && sizesAgree && used == 0;
}

// Same writes as the pool's own init (0xBF4A10): a fresh, empty pool on the new block.
void rebase(uintptr_t pool, void* block, uint32_t size) {
    dwordField(pool, sr2::kPoolBaseOffset) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block));
    dwordField(pool, sr2::kPoolSizeOffset) = size;
    dwordField(pool, sr2::kPoolLimitOffset) = size;
    dwordField(pool, sr2::kPoolCapacityOffset) = size;
    dwordField(pool, sr2::kPoolUsedOffset) = 0;
    dwordField(pool, sr2::kPoolMarkOffset) = 0;
}

}  // namespace

namespace coop_pools {

bool resize(uint32_t maxPlayers, bool vehiclePool) {
    if (g_resized) return true;
    if (maxPlayers <= sr2::kCoopDefaultPlayers) return false;

    uint32_t oldSizes[kPoolCount] = {};
    uint32_t newSizes[kPoolCount] = {};
    void* blocks[kPoolCount] = {};
    bool ok = true;
    for (size_t i = 0; i < kPoolCount && ok; ++i) {
        ok = looksLikePool(kPools[i]);
        if (!ok) break;
        oldSizes[i] = dwordField(kPools[i].address, sr2::kPoolSizeOffset);
        newSizes[i] = kPools[i].newSize(oldSizes[i], maxPlayers, vehiclePool);
        blocks[i] = newSizes[i] ? VirtualAlloc(nullptr, newSizes[i], MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) : nullptr;
        if (!blocks[i]) {
            logger::write("pools: %s not resized, %u bytes unavailable", kPools[i].name, newSizes[i]);
            ok = false;
        }
    }
    if (!ok) {
        for (void* block : blocks) {
            if (block) VirtualFree(block, 0, MEM_RELEASE);
        }
        return false;
    }

    for (size_t i = 0; i < kPoolCount; ++i) {
        rebase(kPools[i].address, blocks[i], newSizes[i]);
        logger::write("pools: %s %u -> %u bytes", kPools[i].name, oldSizes[i], newSizes[i]);
    }
    g_resized = true;
    return true;
}

}  // namespace coop_pools
