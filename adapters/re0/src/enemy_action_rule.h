#pragma once
#include <cstdint>

// Pure rule of the enemy animation match (no game access, unit tested). An enemy's behaviour is the record
// {state, action id, a, b} the class's setAction (vtable slot 63) stores at +0x67a4; the class's own handlers turn a
// new record into a motion. A puppet is told the owner's record through that same call, but only when the owner's
// record has settled, the puppet's own differs for a while, and the last request is not recent: its local AI keeps
// choosing too, and answering every flip would restart motions forever.
namespace enemy_action_rule {

constexpr int64_t kOwnerStableMs = 120;  // the owner's record must have been the same this long
constexpr int64_t kMismatchMs = 200;     // the puppet must have differed from it this long
constexpr int64_t kCooldownMs = 400;     // minimum time between two requests for one enemy
constexpr int64_t kNeverMs = -1000000000;
constexpr int kWords = 4;

struct Action {
    int32_t word[kWords] = {};

    bool operator==(const Action& other) const {
        for (int i = 0; i < kWords; ++i) {
            if (word[i] != other.word[i]) return false;
        }
        return true;
    }
    bool operator!=(const Action& other) const { return !(*this == other); }
};

class Sync {
public:
    void reset() { *this = Sync(); }

    // The owner's record from a snapshot.
    void observeOwner(const Action& owner, int64_t nowMs) {
        if (!hasOwner_ || owner != owner_) ownerSinceMs_ = nowMs;
        owner_ = owner;
        hasOwner_ = true;
    }

    // True when the puppet (whose own record is `local`) should now be told the owner's.
    bool due(const Action& local, int64_t nowMs) {
        if (!hasOwner_ || local == owner_) {
            mismatchSinceMs_ = kNeverMs;
            return false;
        }
        if (mismatchSinceMs_ == kNeverMs) mismatchSinceMs_ = nowMs;
        if (nowMs - ownerSinceMs_ < kOwnerStableMs || nowMs - mismatchSinceMs_ < kMismatchMs ||
            nowMs - lastRequestMs_ < kCooldownMs) {
            return false;
        }
        lastRequestMs_ = nowMs;
        return true;
    }

    const Action& owner() const { return owner_; }

private:
    Action owner_;
    bool hasOwner_ = false;
    int64_t ownerSinceMs_ = 0;
    int64_t mismatchSinceMs_ = kNeverMs;
    int64_t lastRequestMs_ = kNeverMs;
};

}  // namespace enemy_action_rule
