#include "spawner_lock.h"

#include <windows.h>

#include <mutex>
#include <vector>

#include "guest_call.h"
#include "hooks.h"
#include "log.h"
#include "memory.h"
#include "rate_limit.h"
#include "spawner_rules.h"
#include "sr2_addresses.h"

namespace {

// The create takes 7 stack arguments (every caller pops 0x1C bytes); the game reads the first 6.
using GroupCreateFn = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

bool g_trace = false;
GroupCreateFn g_original = nullptr;
std::mutex g_logMutex;
rate_limit::RateLimit g_rateLimit;
uint32_t g_sweptSinceLine = 0;

// The groups of the active list, each with the element slots its template says it has.
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

// Frees every partial group through the game's own teardown; returns how many.
uint32_t sweepPartialGroups() {
    const std::vector<uintptr_t> partial = spawner_rules::partialGroups(activeGroups());
    for (const uintptr_t group : partial) guest_call::call(sr2::kGroupTeardown, {.esi = static_cast<uint32_t>(group)}, {0, 0});
    return static_cast<uint32_t>(partial.size());
}

void logCreate(bool held, uint32_t swept) {
    uint32_t creates = 0;
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_sweptSinceLine += swept;
        if (!g_rateLimit.allow(GetTickCount64(), creates)) return;
        swept = g_sweptSinceLine;
        g_sweptSinceLine = 0;
    }
    logger::write("trace: spawner: group create from thread %lu (lock already held: %s), swept %u partial (%u creates since the last line)",
                  GetCurrentThreadId(), held ? "y" : "n", swept, creates);
}

// The group create under the tick lock.
uint32_t __cdecl atomicGroupCreate(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) {
    auto* lock = reinterpret_cast<CRITICAL_SECTION*>(sr2::kGroupTickLock);
    EnterCriticalSection(lock);
    const bool held = lock->RecursionCount > 1;
    const uint32_t result = g_original(a1, a2, a3, a4, a5, a6, a7);
    const uint32_t swept = spawner_rules::mayFree(lock->RecursionCount) ? sweepPartialGroups() : 0;
    LeaveCriticalSection(lock);
    if (g_trace) logCreate(held, swept);
    return result;
}

}  // namespace

namespace spawner_lock {

void setTrace(bool trace) { g_trace = trace; }

bool installHook() {
    return hooks::install("spawner_group_create", sr2::kGroupCreate, reinterpret_cast<void*>(&atomicGroupCreate),
                          reinterpret_cast<void**>(&g_original));
}

void removeHook() { hooks::remove(sr2::kGroupCreate); }

}  // namespace spawner_lock
