#include "coop_pools.h"

#include <windows.h>

#include <cstring>

#include "log.h"
#include "pool_size.h"
#include "sr2_addresses.h"

namespace {

struct PoolSpec {
    const char* name;
    uintptr_t address;
};

constexpr PoolSpec kPools[] = {
    {"coop_player cpu", sr2::kCoopPlayerCpuPool},
    {"coop_player gpu", sr2::kCoopPlayerGpuPool},
};
constexpr size_t kPoolCount = sizeof(kPools) / sizeof(kPools[0]);

bool g_resized = false;

uint32_t& field(uintptr_t pool, uintptr_t offset) { return *reinterpret_cast<uint32_t*>(pool + offset); }

// The pool carries the expected name and one consistent size.
bool looksLikePool(const PoolSpec& spec) {
    const char* name = reinterpret_cast<const char*>(spec.address + sr2::kPoolNameOffset);
    const uint32_t size = field(spec.address, sr2::kPoolSizeOffset);
    const bool nameMatches = strncmp(name, spec.name, sr2::kPoolNameLength) == 0;
    const bool sizesAgree = size != 0 && size == field(spec.address, sr2::kPoolLimitOffset) &&
                            size == field(spec.address, sr2::kPoolCapacityOffset);
    if (!nameMatches || !sizesAgree) {
        logger::write("pools: %s not resized, object at 0x%X does not match (name \"%.32s\", size %u)", spec.name,
                      static_cast<unsigned>(spec.address), name, size);
    }
    return nameMatches && sizesAgree;
}

// Same writes as the pool's own init (0xBF4A10): a fresh, empty pool on the new block.
void rebase(uintptr_t pool, void* block, uint32_t size) {
    field(pool, sr2::kPoolBaseOffset) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block));
    field(pool, sr2::kPoolSizeOffset) = size;
    field(pool, sr2::kPoolLimitOffset) = size;
    field(pool, sr2::kPoolCapacityOffset) = size;
    field(pool, sr2::kPoolUsedOffset) = 0;
    field(pool, sr2::kPoolMarkOffset) = 0;
}

}  // namespace

namespace coop_pools {

void resize(uint32_t maxPlayers) {
    if (g_resized || maxPlayers <= sr2::kCoopDefaultPlayers) return;

    uint32_t oldSizes[kPoolCount] = {};
    uint32_t newSizes[kPoolCount] = {};
    void* blocks[kPoolCount] = {};
    bool ok = true;
    for (size_t i = 0; i < kPoolCount && ok; ++i) {
        ok = looksLikePool(kPools[i]);
        if (!ok) break;
        oldSizes[i] = field(kPools[i].address, sr2::kPoolSizeOffset);
        newSizes[i] = scaledPoolSize(oldSizes[i], maxPlayers);
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
        return;
    }

    for (size_t i = 0; i < kPoolCount; ++i) {
        rebase(kPools[i].address, blocks[i], newSizes[i]);
        logger::write("pools: %s %u -> %u bytes", kPools[i].name, oldSizes[i], newSizes[i]);
    }
    g_resized = true;
}

}  // namespace coop_pools
