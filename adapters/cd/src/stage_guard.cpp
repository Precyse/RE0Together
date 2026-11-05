#include "stage_guard.h"

#include <atomic>

#include "build_gate.h"
#include "cd_sites.h"
#include "hooks.h"
#include "log.h"
#include "safe_read.h"

namespace {

using Lookup = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

// The stage manager's table (disassembly of 0x142156CA0) [V].
constexpr uint32_t kBucketCount = 0x130;
constexpr uint32_t kBuckets = 0x140;
constexpr uint32_t kEntries = 0x148;
constexpr uint32_t kBucketStride = 0x100;
constexpr uint32_t kBucketPairs = 0x08;  // u32 n at +0, then (u32 hash, u32 entry index) pairs
constexpr uint32_t kPairSize = 8;
constexpr uint32_t kMaxPairs = (kBucketStride - kBucketPairs) / kPairSize;
constexpr uint32_t kMaxBuckets = 1 << 16;
constexpr uint32_t kEntryInstance = 0x08;
constexpr uint32_t kEntryStage = 0x10;
constexpr uint32_t kStageDead = 0x15;
// The branch table (0x140BAD200 and 0x140BA8FD0 index it the same way) [V], its count [I].
constexpr uint32_t kStageChart = 0x60;
constexpr uint32_t kChartBranches = 0x38;
constexpr uint32_t kBranchEntries = 0x08;
constexpr uint32_t kBranchCount = 0x10;
constexpr uint32_t kMaxBranches = 4096;
constexpr uint32_t kBranchKey = 0x10;  // the field the game reads from a branch entry

Lookup g_lookup = nullptr;
std::atomic<uintptr_t> g_manager{0};

uint64_t __fastcall lookupDetour(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7,
                                 uint64_t a8) {
    g_manager.store(static_cast<uintptr_t>(a1), std::memory_order_relaxed);
    return g_lookup(a1, a2, a3, a4, a5, a6, a7, a8);
}

// The live stage of an instance, scanning every bucket (no need for the game's hash).
uintptr_t stageOf(uintptr_t manager, uint64_t instance) {
    uint32_t buckets = 0;
    const uintptr_t bucketBase = safe_read::pointer(manager + kBuckets), entries = safe_read::pointer(manager + kEntries);
    if (!safe_read::value(manager + kBucketCount, buckets) || !bucketBase || !entries || buckets > kMaxBuckets) return 0;
    for (uint32_t b = 0; b < buckets; ++b) {
        const uintptr_t bucket = bucketBase + static_cast<uintptr_t>(b) * kBucketStride;
        uint32_t pairs = 0;
        if (!safe_read::value(bucket, pairs) || pairs > kMaxPairs) continue;
        for (uint32_t p = 0; p < pairs; ++p) {
            uint32_t index = 0;
            uint64_t id = 0;
            if (!safe_read::value(bucket + kBucketPairs + p * kPairSize + sizeof(uint32_t), index)) continue;
            const uintptr_t entry = safe_read::pointer(entries + static_cast<uintptr_t>(index) * sizeof(uintptr_t));
            if (!entry || !safe_read::value(entry + kEntryInstance, id) || id != instance) continue;
            const uintptr_t stage = safe_read::pointer(entry + kEntryStage);
            uint8_t dead = 1;
            return stage && safe_read::value(stage + kStageDead, dead) && !dead ? stage : 0;
        }
    }
    return 0;
}

}  // namespace

namespace stage_guard {

bool install() {
    if (!build_gate::setIntact(cd_sites::kStory)) {
        logger::write("stage_guard: not patched (site bytes differ)");
        return false;
    }
    const bool ok = hooks::install(cd_sites::kStageLookup.name, cd_sites::kStageLookup.address,
                                   reinterpret_cast<void*>(&lookupDetour), reinterpret_cast<void**>(&g_lookup));
    logger::write(ok ? "stage_guard: 1 patched" : "stage_guard: not patched (hook install failed)");
    return ok;
}

void reset() { g_manager.store(0, std::memory_order_relaxed); }

std::optional<Table> table(uint64_t instance) {
    const uintptr_t manager = g_manager.load(std::memory_order_relaxed);
    const uintptr_t stage = manager ? stageOf(manager, instance) : 0;
    const uintptr_t chart = stage ? safe_read::pointer(stage + kStageChart) : 0;
    const uintptr_t branches = chart ? safe_read::pointer(chart + kChartBranches) : 0;
    Table out;
    out.entries = branches ? safe_read::pointer(branches + kBranchEntries) : 0;
    if (!out.entries || !safe_read::value(branches + kBranchCount, out.count) || out.count > kMaxBranches) return std::nullopt;
    return out;
}

bool branchOk(const Table& table, uint32_t branch) {
    if (branch >= table.count) return false;
    const uintptr_t entry = safe_read::pointer(table.entries + static_cast<uintptr_t>(branch) * sizeof(uintptr_t));
    uint32_t key = 0;
    return entry && safe_read::value(entry + kBranchKey, key);
}

}  // namespace stage_guard
