#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

// The pure rules of the object hand-over target (milestone 4 O3, milestone 9 W6a; unit tested). The host hands an object it
// owns but has not loaded to a remote that has its chunk (the game asks the chunk query of the remote). With several remotes
// the nearest one need not be the one that has the chunk; handed to a remote without it the object is handed straight back
// on the next tick. So: the nearest remote whose chunk is loaded, and the nearest when none has it.
namespace handoff_rules {

constexpr uint16_t kNoChunk = 0xBEEF;  // a world object that belongs to no chunk

struct Candidate {
    uintptr_t member;
    float distanceSquared;  // from the object
};

inline void nearestFirst(Candidate* candidates, size_t count) {
    std::stable_sort(candidates, candidates + count,
                     [](const Candidate& a, const Candidate& b) { return a.distanceSquared < b.distanceSquared; });
}

// `candidates` nearest first; `hasChunk(member)` is the expensive query and is asked in order until one says yes. `fallback`
// is the answer when none does (or the object has no chunk): the nearest remote, else whatever the caller has.
template <typename HasChunk>
uintptr_t target(const Candidate* candidates, size_t count, uint16_t chunk, HasChunk hasChunk, uintptr_t fallback) {
    if (count == 0) return fallback;
    if (chunk != kNoChunk) {
        for (size_t i = 0; i < count; ++i) {
            if (hasChunk(candidates[i].member)) return candidates[i].member;
        }
    }
    return candidates[0].member;
}

}  // namespace handoff_rules
