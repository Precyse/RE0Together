#include "spawn_budget.h"

#include <windows.h>

#include <vector>

#include "hooks.h"
#include "log.h"
#include "memory.h"
#include "rate_limit.h"
#include "spawn_budget_rules.h"
#include "sr2_addresses.h"

namespace {

// The group create's 7 stack arguments (its callers pop them); the first is the group set it builds.
using GroupCreateFn = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

bool g_trace = false;
GroupCreateFn g_groupCreate = nullptr;
rate_limit::RateLimit g_refusedLimit;
rate_limit::RateLimit g_partialLimit;

uint64_t millisNow() { return GetTickCount64(); }

// The entries of a group set: count at +0, array at +4.
std::vector<spawn_budget_rules::GroupEntry> groupEntries(uintptr_t groupSet) {
    std::vector<spawn_budget_rules::GroupEntry> entries;
    const uint32_t count = readDword(groupSet + sr2::kGroupSetCountOffset);
    const uintptr_t array = readDword(groupSet + sr2::kGroupSetEntriesOffset);
    for (uint32_t i = 0; i < count && i < sr2::kMaxSpawnGroups; ++i) {
        const uintptr_t entry = array + i * sr2::kGroupSetEntryStride;
        entries.push_back({readDword(entry + sr2::kEntryElementsOffset), readDword(entry + sr2::kEntryExtraOffset) != 0});
    }
    return entries;
}

// The free nodes of a pool's circular free list, counted up to `limit` (enough to know a create fits).
uint32_t freeNodes(uintptr_t headGlobal, uint32_t limit) {
    const uintptr_t head = readDword(headGlobal);
    uint32_t count = 0;
    for (uintptr_t node = head; node != 0 && count < limit;) {
        ++count;
        node = readDword(node + sr2::kPoolLinkOffset);
        if (node == head) break;
    }
    return count;
}

// The active groups (circular), each with the element slots its template says it has.
std::vector<spawn_budget_rules::Group> activeGroups() {
    std::vector<spawn_budget_rules::Group> groups;
    const uintptr_t head = readDword(sr2::kGroupListHead);
    uintptr_t group = head;
    for (size_t i = 0; group != 0 && i < sr2::kMaxSpawnGroups; ++i) {
        spawn_budget_rules::Group entry{group, {}};
        const uintptr_t groupTemplate = readDword(group + sr2::kGroupTemplateOffset);
        uint8_t slotCount = 0;
        if (groupTemplate) readMemory(groupTemplate + sr2::kTemplateSlotCountOffset, &slotCount, sizeof(slotCount));
        for (uint8_t slot = 0; slot < slotCount; ++slot) {
            entry.slots.push_back(readDword(group + sr2::kGroupSlotsOffset + slot * sizeof(uint32_t)));
        }
        groups.push_back(std::move(entry));
        group = readDword(group + sr2::kPoolLinkOffset);
        if (group == head) break;
    }
    return groups;
}

void traceRefused(const spawn_budget_rules::Need& need, uint32_t freeGroups, uint32_t freeElements) {
    uint32_t refused = 0;
    if (!g_refusedLimit.allow(millisNow(), refused)) return;
    logger::write("trace: spawn budget: group create refused, needs %u groups and %u elements, %u and %u free (%u refused since the last line)",
                  need.groups, need.elements, freeGroups, freeElements, refused);
}

void tracePartialGroups() {
    const std::vector<uintptr_t> partial = spawn_budget_rules::partialGroups(activeGroups());
    uint32_t lines = 0;
    if (partial.empty() || !g_partialLimit.allow(millisNow(), lines)) return;
    logger::write("trace: spawn budget: %zu partial group(s) after a group create, first 0x%X (%u lines since the last)", partial.size(),
                  static_cast<unsigned>(partial.front()), lines);
}

uint32_t __cdecl budgetedGroupCreate(uint32_t groupSet, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) {
    if (groupSet != 0) {
        const spawn_budget_rules::Need need = spawn_budget_rules::needFor(groupEntries(groupSet));
        const uint32_t freeGroups = freeNodes(sr2::kGroupFreeHead, need.groups);
        const uint32_t freeElements = freeNodes(sr2::kElementFreeHead, need.elements);
        if (!spawn_budget_rules::fits(need, freeGroups, freeElements)) {
            if (g_trace) traceRefused(need, freeGroups, freeElements);
            return 0;
        }
    }
    const uint32_t group = g_groupCreate(groupSet, a2, a3, a4, a5, a6, a7);
    if (g_trace) tracePartialGroups();
    return group;
}

}  // namespace

namespace spawn_budget {

void setTrace(bool trace) { g_trace = trace; }

bool installHook() {
    return hooks::install("spawn_budget_group_create", sr2::kGroupCreate, reinterpret_cast<void*>(&budgetedGroupCreate),
                          reinterpret_cast<void**>(&g_groupCreate));
}

void removeHook() { hooks::remove(sr2::kGroupCreate); }

}  // namespace spawn_budget
