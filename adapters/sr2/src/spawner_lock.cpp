#include "spawner_lock.h"

#include <windows.h>

#include <intrin.h>

#include <mutex>
#include <vector>

#include "hooks.h"
#include "log.h"
#include "memory.h"
#include "rate_limit.h"
#include "spawner_rules.h"
#include "sr2_addresses.h"

namespace {

using spawner_rules::Site;

// The stack arguments of the entries, from their callers (each pops them after the call): 7, 2, 1 and 2 dwords.
using GroupCreateFn = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using ChunkParseFn = uint32_t(__cdecl*)(uint32_t, uint32_t);
using ChunkUnloadFn = uint32_t(__cdecl*)(uint32_t);
using CrewFreeFn = uint32_t(__cdecl*)(uint32_t, uint32_t);

constexpr uint64_t kMicrosPerSecond = 1000000;

bool g_trace = false;
GroupCreateFn g_groupCreate = nullptr;
ChunkParseFn g_chunkParse = nullptr;
ChunkUnloadFn g_chunkUnload = nullptr;
CrewFreeFn g_crewFree = nullptr;

std::mutex g_logMutex;
rate_limit::RateLimit g_entryLimits[spawner_rules::kSiteCount];
rate_limit::RateLimit g_waitLimit;
rate_limit::RateLimit g_partialLimit;

CRITICAL_SECTION& tickLock() { return *reinterpret_cast<CRITICAL_SECTION*>(sr2::kGroupTickLock); }

uint64_t microsNow() {
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return static_cast<uint64_t>(counter.QuadPart) * kMicrosPerSecond / static_cast<uint64_t>(frequency.QuadPart);
}

uint64_t millisNow() { return GetTickCount64(); }

// The groups of the active list (circular), each with the element slots its template says it has.
std::vector<spawner_rules::Group> activeGroups() {
    std::vector<spawner_rules::Group> groups;
    const uintptr_t head = readDword(sr2::kGroupListHead);
    uintptr_t group = head;
    for (size_t i = 0; group != 0 && i < sr2::kMaxSpawnGroups; ++i) {
        spawner_rules::Group entry{group, {}};
        const uintptr_t groupTemplate = readDword(group + sr2::kGroupTemplateOffset);
        uint8_t slotCount = 0;
        if (groupTemplate) readMemory(groupTemplate + sr2::kTemplateSlotCountOffset, &slotCount, sizeof(slotCount));
        for (uint8_t slot = 0; slot < slotCount; ++slot) {
            entry.slots.push_back(readDword(group + sr2::kGroupSlotsOffset + slot * sizeof(uint32_t)));
        }
        groups.push_back(std::move(entry));
        group = readDword(group + sr2::kGroupNextOffset);
        if (group == head) break;
    }
    return groups;
}

// The tick lock around one call of a game entry: taken on construction, released on destruction.
class TickLock {
public:
    TickLock(Site site, uintptr_t caller) : site_(site) {
        const uint64_t start = g_trace ? microsNow() : 0;
        EnterCriticalSection(&tickLock());
        held_ = tickLock().RecursionCount > 1;
        outermost_ = spawner_rules::outermost(tickLock().RecursionCount);
        if (g_trace) traceEntry(start, caller);
    }

    ~TickLock() { LeaveCriticalSection(&tickLock()); }

    TickLock(const TickLock&) = delete;
    TickLock& operator=(const TickLock&) = delete;

    // True for a call from outside the tick: the group list is at rest while this thread holds the lock.
    bool outermost() const { return outermost_; }

private:
    void traceEntry(uint64_t startMicros, uintptr_t caller) const {
        const uint64_t waited = microsNow() - startMicros;
        uint32_t entries = 0;
        uint32_t waits = 0;
        bool logEntry = false;
        bool logWait = false;
        {
            std::lock_guard<std::mutex> lock(g_logMutex);
            logEntry = g_entryLimits[static_cast<size_t>(site_)].allow(millisNow(), entries);
            logWait = spawner_rules::isLongWait(waited) && g_waitLimit.allow(millisNow(), waits);
        }
        if (logEntry) {
            logger::write("trace: spawner: %s from thread %lu (lock already held: %s), %u since the last line",
                          spawner_rules::siteName(site_), GetCurrentThreadId(), held_ ? "y" : "n", entries);
        }
        if (logWait) {
            logger::write("trace: spawner: %s waited %llu ms for the tick lock on thread %lu, called from 0x%X (%u waits since the last line)",
                          spawner_rules::siteName(site_), waited / 1000, GetCurrentThreadId(), static_cast<unsigned>(caller), waits);
        }
    }

    Site site_;
    bool held_ = false;
    bool outermost_ = false;
};

// After a group create, outside the tick: the groups that are still partial. Only logged; the lock makes the list quiet, so
// a partial group here is one a failed create left behind.
void tracePartialGroups() {
    const std::vector<uintptr_t> partial = spawner_rules::partialGroups(activeGroups());
    if (partial.empty()) return;
    uint32_t lines = 0;
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (!g_partialLimit.allow(millisNow(), lines)) return;
    }
    logger::write("trace: spawner: %zu partial group(s) after a group create, first 0x%X (%u lines since the last)", partial.size(),
                  static_cast<unsigned>(partial.front()), lines);
}

uint32_t __cdecl lockedGroupCreate(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) {
    TickLock lock(Site::GroupCreate, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    const uint32_t result = g_groupCreate(a1, a2, a3, a4, a5, a6, a7);
    if (g_trace && lock.outermost()) tracePartialGroups();
    return result;
}

uint32_t __cdecl lockedChunkParse(uint32_t a1, uint32_t a2) {
    TickLock lock(Site::ChunkParse, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    return g_chunkParse(a1, a2);
}

uint32_t __cdecl lockedChunkUnload(uint32_t a1) {
    TickLock lock(Site::ChunkUnload, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    return g_chunkUnload(a1);
}

uint32_t __cdecl lockedCrewFree(uint32_t a1, uint32_t a2) {
    TickLock lock(Site::CrewFree, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    return g_crewFree(a1, a2);
}

struct HookSite {
    const char* name;
    uintptr_t address;
    void* detour;
    void** original;
};

const HookSite kHookSites[] = {
    {"spawner_group_create", sr2::kGroupCreate, reinterpret_cast<void*>(&lockedGroupCreate), reinterpret_cast<void**>(&g_groupCreate)},
    {"spawner_chunk_parse", sr2::kChunkParse, reinterpret_cast<void*>(&lockedChunkParse), reinterpret_cast<void**>(&g_chunkParse)},
    {"spawner_chunk_unload", sr2::kChunkUnload, reinterpret_cast<void*>(&lockedChunkUnload), reinterpret_cast<void**>(&g_chunkUnload)},
    {"spawner_crew_free", sr2::kCrewFree, reinterpret_cast<void*>(&lockedCrewFree), reinterpret_cast<void**>(&g_crewFree)},
};

}  // namespace

namespace spawner_lock {

void setTrace(bool trace) { g_trace = trace; }

bool installHook() {
    for (const HookSite& site : kHookSites) {
        if (hooks::install(site.name, site.address, site.detour, site.original)) continue;
        removeHook();
        return false;
    }
    return true;
}

void removeHook() {
    for (const HookSite& site : kHookSites) hooks::remove(site.address);
}

}  // namespace spawner_lock
