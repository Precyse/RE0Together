#pragma once
// Pure bookkeeping of the shared locker (no game): the guest's moves that wait for the host's answer, and the host's rate
// limit on one guest's moves.
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "locker_wire.h"
#include "time_us.h"

namespace locker_table {

constexpr uint64_t kTimeoutMs = 10'000;       // a move the host never answered stops holding its locker
constexpr uint32_t kMaxMovesPerSecond = 20;   // the host refuses more than this from one guest
constexpr TimeUs kWindowUs = 1'000'000;

// The moves a guest sent and has no answer to yet. A locker with a move waiting is not brought to the host's list: the
// list that answers the move comes first, then the result clears the wait.
class PendingMoves {
public:
    // Registers `move` (its request id is set here, from 1) and returns it as it goes on the wire.
    locker_wire::LockerMove add(locker_wire::LockerMove move, uint64_t nowMs) {
        move.requestId = ++lastId_;
        waiting_[move.requestId] = {move, nowMs};
        return move;
    }

    bool pending(uint32_t key, uint8_t kind) const {
        for (const auto& [id, entry] : waiting_) {
            if (entry.move.key == key && entry.move.slotKind == kind) return true;
        }
        return false;
    }

    // The move the host answered, or nothing for an id that is not waiting (late, duplicate or invented).
    std::optional<locker_wire::LockerMove> resolve(uint32_t requestId) {
        const auto found = waiting_.find(requestId);
        if (found == waiting_.end()) return std::nullopt;
        const locker_wire::LockerMove move = found->second.move;
        waiting_.erase(found);
        return move;
    }

    // Forgets the moves waiting longer than kTimeoutMs and counts them.
    void expire(uint64_t nowMs) {
        std::erase_if(waiting_, [&](const auto& item) {
            const bool late = nowMs - item.second.since > kTimeoutMs;
            expired_ += late ? 1 : 0;
            return late;
        });
    }

    uint64_t expiredTotal() const { return expired_; }
    size_t size() const { return waiting_.size(); }

    void clear() { waiting_.clear(); }

private:
    struct Entry {
        locker_wire::LockerMove move;
        uint64_t since;
    };
    std::map<uint32_t, Entry> waiting_;
    uint32_t lastId_ = 0;
    uint64_t expired_ = 0;
};

// One sender's moves per one-second window.
class MoveLimiter {
public:
    bool allow(TimeUs now) {
        if (now - windowStart_ >= kWindowUs || now < windowStart_) {
            windowStart_ = now;
            count_ = 0;
        }
        return ++count_ <= kMaxMovesPerSecond;
    }

private:
    TimeUs windowStart_ = 0;
    uint32_t count_ = 0;
};

}  // namespace locker_table
