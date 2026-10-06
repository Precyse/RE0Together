#pragma once
#include <cstdint>

// Pure rule of the enemy AI replication (no game access, unit tested). An enemy's behaviour is the record
// {state, action id, a, b} the class's setAction (vtable slot 63) stores at +0x67a4; the class's own handlers turn a
// new record into a motion. On a puppet the enemy's own setAction calls are refused, so the owner's record is applied
// as soon as it differs; the cooldown only covers handlers that write the state word directly (they bypass setAction).
namespace enemy_action_rule {

constexpr int64_t kCooldownMs = 100;  // minimum time between two applications for one enemy
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
    void observeOwner(const Action& owner) {
        owner_ = owner;
        hasOwner_ = true;
    }

    // True when the puppet (whose own record is `local`) should now be given the owner's.
    bool due(const Action& local, int64_t nowMs) {
        if (!hasOwner_ || local == owner_ || nowMs - lastApplyMs_ < kCooldownMs) return false;
        lastApplyMs_ = nowMs;
        return true;
    }

    const Action& owner() const { return owner_; }

private:
    Action owner_;
    bool hasOwner_ = false;
    int64_t lastApplyMs_ = kNeverMs;
};

}  // namespace enemy_action_rule
