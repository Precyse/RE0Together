#pragma once
// The host's wait for its guests before a cutscene starts: each announced cutscene waits until every peer it was announced
// to has answered READY, or until the timeout (a guest that cannot ready must not hold the host's story forever).
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace cutscene_gate {

constexpr uint64_t kReadyTimeoutMs = 5000;

struct Opened {
    uint32_t id;
    bool timedOut;
};

class ReadyGate {
public:
    void begin(uint32_t id, std::set<uint8_t> peers, uint64_t nowMs) {
        waiting_[id] = {std::move(peers), nowMs + kReadyTimeoutMs};
    }

    void ready(uint32_t id, uint8_t slot) {
        const auto it = waiting_.find(id);
        if (it != waiting_.end()) it->second.missing.erase(slot);
    }

    // A peer that left no longer holds anything up.
    void drop(uint8_t slot) {
        for (auto& [id, entry] : waiting_) entry.missing.erase(slot);
    }

    // The cutscenes that can start now: all peers ready, or out of time. Each is returned once.
    std::vector<Opened> takeOpened(uint64_t nowMs) {
        std::vector<Opened> opened;
        for (auto it = waiting_.begin(); it != waiting_.end();) {
            const bool allReady = it->second.missing.empty();
            if (allReady || nowMs >= it->second.deadlineMs) {
                opened.push_back({it->first, !allReady});
                it = waiting_.erase(it);
            } else {
                ++it;
            }
        }
        return opened;
    }

private:
    struct Entry {
        std::set<uint8_t> missing;
        uint64_t deadlineMs = 0;
    };
    std::map<uint32_t, Entry> waiting_;
};

}  // namespace cutscene_gate
