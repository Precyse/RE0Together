#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>

// The pure rules of the remote vehicle trace (milestone 12 VT1, unit tested): why a remote vehicle's create failed, and how
// many attempts a net id needed. The create takes a creation data record first and then a vehicle from the static pool; the
// game retries a failed create until its window runs out (docs/SR2_NOTES.md, "Remote vehicles").
namespace vehicle_trace_rules {

constexpr size_t kMaxTrackedIds = 256;  // the game's object_create_data records

enum class Failure { CreationData, Pool, Category, Other };

// What the create reads to say why it failed: the two free list heads (0 = empty) and the vehicle category (class 1) in use, its
// limit and its free node count.
struct CreateState {
    uint32_t creationDataHead;
    uint32_t poolHead;
    uint32_t class1Count;
    uint32_t class1Limit;
    uint32_t freeNodes;
};

// The create asks for the creation data first, so a missing record is the reason before an empty pool; then the category slot
// (class 1 at its limit, or no free category node); anything else is the streaming.
inline Failure classify(const CreateState& state) {
    if (state.creationDataHead == 0) return Failure::CreationData;
    if (state.poolHead == 0) return Failure::Pool;
    if (state.class1Count >= state.class1Limit || state.freeNodes == 0) return Failure::Category;
    return Failure::Other;
}

inline const char* name(Failure failure) {
    switch (failure) {
        case Failure::CreationData: return "creation data";
        case Failure::Pool: return "pool";
        case Failure::Category: return "category";
        default: return "other";
    }
}

// What the census line reports about the creates seen.
struct Totals {
    uint32_t creates = 0;
    uint32_t failed = 0;
    uint32_t pool = 0;
    uint32_t creationData = 0;
    uint32_t category = 0;
    uint32_t other = 0;
    uint32_t droppedAtApply = 0;  // a vehicle received whose object was destroyed again by the apply (its stream container is not loaded)

    void record(bool created, Failure failure) {
        ++creates;
        if (created) return;
        ++failed;
        switch (failure) {
            case Failure::CreationData: ++creationData; break;
            case Failure::Pool: ++pool; break;
            case Failure::Category: ++category; break;
            default: ++other; break;
        }
    }

    void recordDropped() { ++droppedAtApply; }
};

// The failed attempts per net id, for at most kMaxTrackedIds ids (the oldest is forgotten when it is full).
class Attempts {
public:
    // A failed attempt of `id`: how many have failed so far, this one included (1 = the first, which is the one to log).
    uint32_t failed(uint16_t id) {
        auto found = find(id);
        if (found != ids_.end()) return ++found->second;
        if (ids_.size() == kMaxTrackedIds) ids_.pop_front();
        ids_.emplace_back(id, 1);
        return 1;
    }

    // `id` was created: how many attempts failed before (0 = it never failed). The id is forgotten.
    uint32_t created(uint16_t id) {
        auto found = find(id);
        if (found == ids_.end()) return 0;
        const uint32_t before = found->second;
        ids_.erase(found);
        return before;
    }

private:
    using Entry = std::pair<uint16_t, uint32_t>;

    std::deque<Entry>::iterator find(uint16_t id) {
        return std::find_if(ids_.begin(), ids_.end(), [id](const Entry& entry) { return entry.first == id; });
    }

    std::deque<Entry> ids_;
};

}  // namespace vehicle_trace_rules
