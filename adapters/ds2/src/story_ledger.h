#pragma once
// The host's record of the missions it started, with the arguments and start section the script gave: a guest that joins
// (or resyncs) while a mission is in progress gets the real start replayed, so the later success or failure finds its mission
// started and its stage active. A mission the host never saw start (it came from a save) has no entry; its start goes out
// with the no-row fallback.
#include <cstdint>
#include <unordered_map>

#include "story_wire.h"

namespace story_ledger {

constexpr uint32_t kNoRow = 0xFFFFFFFF;  // start request argument: no terminal list row

class Ledger {
public:
    void noteStart(const story_wire::Event& start) { starts_[start.missionId] = start; }
    void noteEnd(uint64_t missionId) { starts_.erase(missionId); }
    void clear() { starts_.clear(); }

    // The start event to replay for a mission in progress.
    story_wire::Event startFor(uint64_t missionId) const {
        const auto found = starts_.find(missionId);
        if (found != starts_.end()) return found->second;
        story_wire::Event fallback{};
        fallback.kind = static_cast<uint8_t>(story_wire::Kind::MissionStart);
        fallback.a = kNoRow;
        fallback.missionId = missionId;
        return fallback;
    }

    size_t size() const { return starts_.size(); }

private:
    std::unordered_map<uint64_t, story_wire::Event> starts_;
};

}  // namespace story_ledger
