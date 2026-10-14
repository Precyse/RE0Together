#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// The pure rules of the spawn group lock (milestone 5i, sections 9 and 10; unit tested).
namespace spawner_rules {

// The four entries of the game that create or free spawn groups and now run under the tick lock.
enum class Site : size_t { GroupCreate, ChunkParse, ChunkUnload, CrewFree };
constexpr size_t kSiteCount = 4;

inline const char* siteName(Site site) {
    switch (site) {
        case Site::GroupCreate: return "group create";
        case Site::ChunkParse: return "chunk parse";
        case Site::ChunkUnload: return "chunk unload";
        case Site::CrewFree: return "crew free";
    }
    return "?";
}

// A spawn group as the trace reads it: its address and the first `slot count` of its element slots.
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

// The partial groups of a list.
inline std::vector<uintptr_t> partialGroups(const std::vector<Group>& groups) {
    std::vector<uintptr_t> partial;
    for (const Group& group : groups) {
        if (isPartial(group)) partial.push_back(group.address);
    }
    return partial;
}

// A thread that took the lock just now and did not hold it already is outside the tick; only then is the list at rest (every
// writer takes the lock), so only then is a look at it meaningful.
inline bool outermost(long recursionCountAfterEnter) { return recursionCountAfterEnter == 1; }

// A wait for the lock that is worth a line in the trace: someone held the lock for longer than a frame's share.
constexpr uint64_t kLongWaitMs = 20;

inline bool isLongWait(uint64_t waitMicros) { return waitMicros > kLongWaitMs * 1000; }

}  // namespace spawner_rules
