#pragma once
// The shared story (pure, unit tested; cd design progression.md 4.2-4.3). The host logs every stage branch its own game
// chose (and that its server confirmed) under the stage's sequencer key, with the branch its instance left (when the
// host knows it); the guest replays them on its own instances.
//
// Host: a log with a random id and dense sequence numbers, at most kMaxEntries, resent whole on every new link.
// Guest: applies each sequence number once; queues per sequencer key; an entry is tried only when the local instance
// exists, is not busy, has the branch in range and sits on the branch the entry starts from (or, when the host did not
// know it, on the branch the previous applied entry of that key left). So a host that reloads an older save never rolls
// the guest back: its replayed branches start from a branch the guest is no longer on, wait and expire. The resume rule
// skips every queued entry up to the LAST one whose branch the local instance already sits on (a guest ahead). A guest
// behind keeps its entries while its world lacks the stage; the expiry runs only while the instance exists.
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <vector>

#include "fnv1a32.h"

namespace story_rule {

constexpr uint32_t kMaxEntries = 4096;
constexpr size_t kMaxPending = 256;
constexpr uint64_t kRetryMs = 5000;
constexpr uint64_t kExpireMs = 120000;
constexpr uint32_t kUnknownBranch = UINT32_MAX;

struct Entry {
    uint32_t seq = 0;
    uint32_t sequencerKey = 0;
    uint32_t fromBranch = kUnknownBranch;  // the branch the host's instance left
    uint32_t branch = 0;
    uint16_t flags = 0;
};

class HostLog {
public:
    explicit HostLog(uint32_t logId) : logId_(logId) {}

    // Null when the log is full.
    std::optional<Entry> append(uint32_t sequencerKey, uint32_t fromBranch, uint32_t branch, uint16_t flags) {
        if (entries_.size() >= kMaxEntries) return std::nullopt;
        entries_.push_back({static_cast<uint32_t>(entries_.size() + 1), sequencerKey, fromBranch, branch, flags});
        return entries_.back();
    }
    uint32_t logId() const { return logId_; }
    const std::vector<Entry>& entries() const { return entries_; }

private:
    uint32_t logId_;
    std::vector<Entry> entries_;
};

// The guest's stage instance for a sequencer key, as its own game shows it now.
struct Local {
    bool exists = false;
    bool busy = false;                     // its own request is newer than the last Ack that carried a branch
    std::optional<uint32_t> sittingBranch;
    uint32_t branchCount = 0;              // 0 when the branch table is missing
};

class GuestLog {
public:
    // A STORY_LOG_BEGIN (or any entry) of another log id starts over.
    void begin(uint32_t logId) {
        if (logId == logId_) return;
        logId_ = logId;
        seen_.clear();
        queues_.clear();
        lastBranch_.clear();
        pending_ = 0;
    }

    // False when the entry is a duplicate or the queue is full (dropped, counted by the caller).
    bool offer(const Entry& entry) {
        if (entry.seq == 0 || seen_.count(entry.seq) || pending_ >= kMaxPending) return false;
        seen_.insert(entry.seq);
        auto& queue = queues_[entry.sequencerKey];
        const auto at = std::find_if(queue.begin(), queue.end(), [&](const Waiting& w) { return w.entry.seq > entry.seq; });
        queue.insert(at, {entry, std::nullopt, std::nullopt});
        ++pending_;
        return true;
    }

    // The entry to try now for this key, or null. A returned entry is not offered again for kRetryMs; it leaves the
    // queue when applied() confirms it, or kExpireMs after the instance first existed while it was at the head.
    std::optional<Entry> next(uint32_t sequencerKey, const Local& local, uint64_t nowMs) {
        auto it = queues_.find(sequencerKey);
        if (it == queues_.end()) return std::nullopt;
        auto& queue = it->second;
        if (!local.exists) return std::nullopt;
        if (local.sittingBranch) skipThrough(sequencerKey, queue, *local.sittingBranch);
        while (!queue.empty() && queue.front().headSinceMs && nowMs - *queue.front().headSinceMs >= kExpireMs) drop(queue);
        if (queue.empty()) return std::nullopt;
        Waiting& head = queue.front();
        if (!head.headSinceMs) head.headSinceMs = nowMs;
        if (local.busy || head.entry.branch >= local.branchCount || !chained(sequencerKey, head.entry, local)) return std::nullopt;
        if (head.lastTryMs && nowMs - *head.lastTryMs < kRetryMs) return std::nullopt;
        head.lastTryMs = nowMs;
        return head.entry;
    }

    // The server confirmed the branch of `seq` on our instance.
    void applied(uint32_t sequencerKey, uint32_t seq) {
        auto it = queues_.find(sequencerKey);
        if (it == queues_.end() || it->second.empty() || it->second.front().entry.seq != seq) return;
        lastBranch_[sequencerKey] = it->second.front().entry.branch;
        drop(it->second);
    }

    size_t pending() const { return pending_; }

    // The sequencer keys with entries waiting.
    std::vector<uint32_t> waitingKeys() const {
        std::vector<uint32_t> out;
        for (const auto& [key, queue] : queues_) {
            if (!queue.empty()) out.push_back(key);
        }
        return out;
    }

    // FNV-1a 32 of the last applied branch per sequencer key (what STORY_DIGEST compares).
    uint32_t digest() const {
        uint32_t state = fnv1a32::kOffsetBasis;
        for (const auto& [key, branch] : lastBranch_) {
            state = fnv1a32::hash(std::span(reinterpret_cast<const uint8_t*>(&key), sizeof(key)), state);
            state = fnv1a32::hash(std::span(reinterpret_cast<const uint8_t*>(&branch), sizeof(branch)), state);
        }
        return state;
    }

private:
    struct Waiting {
        Entry entry;
        std::optional<uint64_t> headSinceMs, lastTryMs;
    };

    bool chained(uint32_t sequencerKey, const Entry& entry, const Local& local) const {
        if (entry.fromBranch != kUnknownBranch) return local.sittingBranch == entry.fromBranch;
        const auto previous = lastBranch_.find(sequencerKey);
        return previous == lastBranch_.end() || local.sittingBranch == previous->second;
    }

    void drop(std::deque<Waiting>& queue) {
        queue.pop_front();
        --pending_;
    }

    // Resume rule: the instance already sits on a branch the queue still holds; the last such entry and everything
    // before it are done here (the last, so a loop that comes back to the branch is not replayed).
    void skipThrough(uint32_t sequencerKey, std::deque<Waiting>& queue, uint32_t sitting) {
        const auto at = std::find_if(queue.rbegin(), queue.rend(), [&](const Waiting& w) { return w.entry.branch == sitting; });
        if (at == queue.rend()) return;
        const auto skipped = static_cast<size_t>(std::distance(at, queue.rend()));
        for (size_t i = 0; i < skipped; ++i) drop(queue);
        lastBranch_[sequencerKey] = sitting;
    }

    uint32_t logId_ = 0;
    std::set<uint32_t> seen_;
    std::map<uint32_t, std::deque<Waiting>> queues_;
    std::map<uint32_t, uint32_t> lastBranch_;
    size_t pending_ = 0;
};

}  // namespace story_rule
