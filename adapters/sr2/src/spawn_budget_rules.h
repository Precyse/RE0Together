#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "population_rules.h"
#include "sr2_addresses.h"
#include "vehicle_trace_rules.h"

// The pure rules of the spawn budget (milestone 5i, section 12; milestone 12: the ambient reserve and back-off) and of the
// pool census (milestone 11 B6; unit tested).
namespace spawn_budget_rules {

// The ambient caller (the child groups spawned around the local camera) may not take the last groups and elements: the
// chunk node loads, vehicle crews and shops, which have no way to wait, find room there. A refused ambient create is not
// asked again for kAmbientRetryMs.
constexpr uint32_t kAmbientReserveGroups = 480;
constexpr uint32_t kAmbientReserveElements = 480;
constexpr uint64_t kAmbientRetryMs = 1000;

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

// The ambient caller is the child group spawn; every other caller is reserved, a caller not known included.
inline bool isAmbientCaller(uintptr_t returnAddress) { return returnAddress == sr2::kGroupCreateReturnA; }

// How far the free list walk has to go to decide: the create's need, and for the ambient caller the reserve above it.
inline uint32_t walkLimit(bool ambient, uint32_t need, uint32_t reserve) { return ambient ? need + reserve : need; }

enum class Verdict { Create, Reserve, NoRoom, Backoff };

// What a group create may do. `freeGroups` and `freeElements` are counted up to walkLimit. Reserve = it would fit without
// the reserve, NoRoom = it does not fit at all, Backoff = the ambient caller is waiting after a refusal.
inline Verdict decide(bool ambient, const Need& need, uint32_t freeGroups, uint32_t freeElements, bool inBackoff) {
    if (ambient && inBackoff) return Verdict::Backoff;
    if (!fits(need, freeGroups, freeElements)) return Verdict::NoRoom;
    const Need withReserve{need.groups + kAmbientReserveGroups, need.elements + kAmbientReserveElements};
    if (ambient && !fits(withReserve, freeGroups, freeElements)) return Verdict::Reserve;
    return Verdict::Create;
}

// A refusal of the ambient caller starts the wait; the wait itself, and the refusal of a reserved caller, do not.
inline bool startsBackoff(bool ambient, Verdict verdict) { return ambient && (verdict == Verdict::Reserve || verdict == Verdict::NoRoom); }

inline bool inBackoff(uint64_t nowMs, uint64_t untilMs) { return nowMs < untilMs; }

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

// The group create's callers, as the census groups them: the ambient child spawn, the two reserved callers it knows (crews
// and shops) and any other reserved one.
enum class Caller { Ambient, Crew, Shop, OtherReserved };

inline Caller callerOf(uintptr_t returnAddress) {
    if (returnAddress == sr2::kGroupCreateReturnA) return Caller::Ambient;
    if (returnAddress == sr2::kGroupCreateReturnB) return Caller::Crew;
    if (returnAddress == sr2::kGroupCreateReturnC) return Caller::Shop;
    return Caller::OtherReserved;
}

// What happened to one group create: the game made the group, returned 0 (Failed), or the budget refused it.
enum class Outcome { Made, Failed, Reserve, Backoff, NoRoom };

inline Outcome outcomeOf(Verdict verdict, bool created) {
    switch (verdict) {
        case Verdict::Reserve: return Outcome::Reserve;
        case Verdict::NoRoom: return Outcome::NoRoom;
        case Verdict::Backoff: return Outcome::Backoff;
        default: return created ? Outcome::Made : Outcome::Failed;
    }
}

// The group creates per caller and outcome.
class GroupCreateCounts {
public:
    struct Count {
        uint32_t made = 0;
        uint32_t failed = 0;
        uint32_t reserve = 0;
        uint32_t backoff = 0;
        uint32_t noRoom = 0;
    };

    void record(Caller caller, Outcome outcome) {
        Count& count = counts_[static_cast<size_t>(caller)];
        switch (outcome) {
            case Outcome::Made: ++count.made; break;
            case Outcome::Failed: ++count.failed; break;
            case Outcome::Reserve: ++count.reserve; break;
            case Outcome::Backoff: ++count.backoff; break;
            case Outcome::NoRoom: ++count.noRoom; break;
        }
    }

    Count at(Caller caller) const { return counts_[static_cast<size_t>(caller)]; }

    // "0x910B58 ambient made 5 reserve hits 2 back-off 90 no room 1 failed 0, 0x912E4C reserved made 3 no room 0 failed 0, ...".
    // The reserve hits are the ambient creates that would have fit without the reserve.
    std::string describe() const {
        char text[384];
        const Count ambient = at(Caller::Ambient);
        int length = std::snprintf(text, sizeof(text), "0x%X ambient made %u reserve hits %u back-off %u no room %u failed %u",
                                   static_cast<unsigned>(sr2::kGroupCreateReturnA), ambient.made, ambient.reserve, ambient.backoff,
                                   ambient.noRoom, ambient.failed);
        char crew[16], shop[16];
        std::snprintf(crew, sizeof(crew), "0x%X", static_cast<unsigned>(sr2::kGroupCreateReturnB));
        std::snprintf(shop, sizeof(shop), "0x%X", static_cast<unsigned>(sr2::kGroupCreateReturnC));
        length += reserved(text + length, sizeof(text) - length, crew, at(Caller::Crew));
        length += reserved(text + length, sizeof(text) - length, shop, at(Caller::Shop));
        reserved(text + length, sizeof(text) - length, "other", at(Caller::OtherReserved));
        return text;
    }

private:
    // ", <caller> reserved made m no room n failed z"
    static int reserved(char* out, size_t size, const char* caller, const Count& count) {
        return std::snprintf(out, size, ", %s reserved made %u no room %u failed %u", caller, count.made, count.noRoom, count.failed);
    }

    std::array<Count, 4> counts_ = {};
};

// The groups and elements the chunk node loader took, per caller (return address); a caller not listed is not counted.
template <size_t N>
class TakenByCaller {
public:
    explicit TakenByCaller(const std::array<uintptr_t, N>& callers) : callers_(callers) {}

    void add(uintptr_t caller, uint32_t groups, uint32_t elements) {
        for (size_t i = 0; i < N; ++i) {
            if (callers_[i] != caller) continue;
            taken_[i].groups += groups;
            taken_[i].elements += elements;
            return;
        }
    }

    // "0x914820 12/30 0xA25035 0/0": the caller, then groups/elements.
    std::string describe() const {
        std::string text;
        char piece[48];
        for (size_t i = 0; i < N; ++i) {
            std::snprintf(piece, sizeof(piece), "%s0x%X %u/%u", i ? " " : "", static_cast<unsigned>(callers_[i]), taken_[i].groups,
                          taken_[i].elements);
            text += piece;
        }
        return text;
    }

private:
    struct Taken {
        uint32_t groups = 0;
        uint32_t elements = 0;
    };

    std::array<uintptr_t, N> callers_;
    std::array<Taken, N> taken_ = {};
};

// What the census line reports.
struct Census {
    uint32_t freeGroups;
    uint32_t freeElements;
    uint32_t groupsInUse;  // the game's own counters, to cross-check the free list walks
    uint32_t elementsInUse;
    uint32_t freeNpcHumans;
    std::string groupCreates;  // GroupCreateCounts::describe
    std::string chunkNodes;    // TakenByCaller::describe of the chunk node loader
    uint32_t freeVehicles;
    uint32_t freeCreationData;
    vehicle_trace_rules::Totals remoteVehicles;
    std::string npcCreates;  // CallerCounts::describe of the NPC human setup: calls/with no human
    size_t remoteHumans;
    size_t remoteMembers;
    uint32_t areasHundredths;  // the player areas of the last population budget, in hundredths; 0 = none evaluated
};

inline std::string censusLine(const Census& census) {
    char line[1536];
    std::snprintf(line, sizeof(line),
                  "trace: census: free groups %u/%zu (in use %u), free elements %u/%zu (in use %u), free npc humans %u/%zu, "
                  "group creates %s, chunk nodes took groups/elements %s, vehicles free %u/%zu creation data %u/%zu, "
                  "remote vehicle creates %u failed %u (pool %u, creation data %u, other %u), "
                  "npc setups/without a human by caller %s, remote humans %zu, remote members %zu, player areas %u.%02u",
                  census.freeGroups, sr2::kMaxSpawnGroups, census.groupsInUse, census.freeElements, sr2::kSpawnElementPool,
                  census.elementsInUse, census.freeNpcHumans, sr2::kNpcHumanPool, census.groupCreates.c_str(),
                  census.chunkNodes.c_str(), census.freeVehicles, sr2::kVehiclePool, census.freeCreationData,
                  sr2::kVehicleCreationPool, census.remoteVehicles.creates, census.remoteVehicles.failed, census.remoteVehicles.pool,
                  census.remoteVehicles.creationData, census.remoteVehicles.other, census.npcCreates.c_str(), census.remoteHumans,
                  census.remoteMembers, census.areasHundredths / population_rules::kAreaHundredths,
                  census.areasHundredths % population_rules::kAreaHundredths);
    return line;
}

}  // namespace spawn_budget_rules
