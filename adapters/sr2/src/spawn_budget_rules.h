#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// The pure rules of the spawn budget (milestone 5i, section 12; unit tested).
namespace spawn_budget_rules {

// One entry of the group set a group create builds: its element count, and whether it also takes the extra element
// (the entry's +0x28 is set).
struct GroupEntry {
    uint32_t elements;
    bool extra;
};

// What a create takes from the two static pools.
struct Need {
    uint32_t groups = 0;
    uint32_t elements = 0;
};

// The create skips an entry without elements; every other entry takes one group, its elements and the extra one.
inline Need needFor(const std::vector<GroupEntry>& entries) {
    Need need;
    for (const GroupEntry& entry : entries) {
        if (entry.elements == 0) continue;
        ++need.groups;
        need.elements += entry.elements + (entry.extra ? 1 : 0);
    }
    return need;
}

// The create runs only when both pools can give everything it takes: a create that runs out midway returns 0 with its
// half-filled group still linked, and the tick reads that group's null slots.
inline bool fits(const Need& need, uint32_t freeGroups, uint32_t freeElements) {
    return need.groups <= freeGroups && need.elements <= freeElements;
}

// A spawn group as the trace reads it: its address and the first `slot count` of its element slots.
struct Group {
    uintptr_t address;
    std::vector<uint32_t> slots;
};

// A group with a null slot below its template's slot count; the tick loops read every one of those slots.
inline bool isPartial(const Group& group) {
    for (const uint32_t slot : group.slots) {
        if (slot == 0) return true;
    }
    return false;
}

// The partial groups of a list.
inline std::vector<uintptr_t> partialGroups(const std::vector<Group>& groups) {
    std::vector<uintptr_t> partial;
    for (const Group& group : groups) {
        if (isPartial(group)) partial.push_back(group.address);
    }
    return partial;
}

}  // namespace spawn_budget_rules
