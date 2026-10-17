#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "population_rules.h"
#include "sr2_addresses.h"

// The pure rules of the spawn budget (milestone 5i, section 12) and of the pool census (milestone 11 B6; unit tested).
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

// The nodes of a circular free list, counted from `head` through `next(node)` and stopping at the head again, at a null link
// or at `limit` (enough to know a create fits, and a bound for a list that does not come back).
template <typename Next>
uint32_t countCircular(uintptr_t head, uint32_t limit, Next next) {
    uint32_t count = 0;
    for (uintptr_t node = head; node != 0 && count < limit;) {
        ++count;
        node = next(node);
        if (node == head) break;
    }
    return count;
}

// How often a function was called from each of a fixed set of callers (by return address), and how often that failed. A call
// from any other address is not counted.
template <size_t N>
class CallerCounts {
public:
    struct Count {
        uint32_t calls = 0;
        uint32_t failed = 0;
    };

    explicit CallerCounts(const std::array<uintptr_t, N>& callers) : callers_(callers) {}

    void record(uintptr_t caller, bool failed) {
        for (size_t i = 0; i < N; ++i) {
            if (callers_[i] != caller) continue;
            ++counts_[i].calls;
            if (failed) ++counts_[i].failed;
            return;
        }
    }

    // "0x910B58 12/0 0x912E4C 7/1": the caller, then calls/failed.
    std::string describe() const {
        std::string text;
        char piece[48];
        for (size_t i = 0; i < N; ++i) {
            std::snprintf(piece, sizeof(piece), "%s0x%X %u/%u", i ? " " : "", static_cast<unsigned>(callers_[i]), counts_[i].calls,
                          counts_[i].failed);
            text += piece;
        }
        return text;
    }

    Count at(size_t index) const { return counts_[index]; }

private:
    std::array<uintptr_t, N> callers_;
    std::array<Count, N> counts_ = {};
};

// What the census line reports.
struct Census {
    uint32_t freeGroups;
    uint32_t freeElements;
    uint32_t freeNpcHumans;
    std::string groupCreates;  // CallerCounts::describe of the group create: calls/refused
    std::string npcCreates;    // of the NPC human setup: calls/with no human
    size_t remoteHumans;
    uint32_t areasHundredths;  // the player areas of the last population budget, in hundredths; 0 = none evaluated
};

inline std::string censusLine(const Census& census) {
    char line[512];
    std::snprintf(line, sizeof(line),
                  "trace: census: free groups %u/%zu, free elements %u/%zu, free npc humans %u/%zu, creates/refused by caller %s, "
                  "npc setups/without a human by caller %s, remote humans %zu, player areas %u.%02u",
                  census.freeGroups, sr2::kMaxSpawnGroups, census.freeElements, sr2::kSpawnElementPool, census.freeNpcHumans,
                  sr2::kNpcHumanPool, census.groupCreates.c_str(), census.npcCreates.c_str(), census.remoteHumans,
                  census.areasHundredths / population_rules::kAreaHundredths, census.areasHundredths % population_rules::kAreaHundredths);
    return line;
}

}  // namespace spawn_budget_rules
