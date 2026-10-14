#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// The pure rules of the ambient spawner's group sweep (milestone 5i, section 9; unit tested).
namespace spawner_rules {

// A spawn group as the sweep reads it: its address and the first `slot count` of its element slots.
struct Group {
    uintptr_t address;
    std::vector<uint32_t> slots;
};

// A group that is still being filled, or was left half filled by a failed create, has a null slot below its template's
// slot count. The game's tick loops read every one of those slots.
inline bool isPartial(const Group& group) {
    for (const uint32_t slot : group.slots) {
        if (slot == 0) return true;
    }
    return false;
}

// The groups the sweep frees.
inline std::vector<uintptr_t> partialGroups(const std::vector<Group>& groups) {
    std::vector<uintptr_t> partial;
    for (const Group& group : groups) {
        if (isPartial(group)) partial.push_back(group.address);
    }
    return partial;
}

// Only a thread that took the tick lock just now, and did not hold it already, may sweep: a thread that holds it already is
// inside the tick, which may be walking the groups it would free. Every creator takes the lock, so at that point no other
// thread is filling a group.
inline bool mayFree(long recursionCountAfterEnter) { return recursionCountAfterEnter == 1; }

}  // namespace spawner_rules
