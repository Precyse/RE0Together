#pragma once
#include <cstdint>

// Pure rule of the owner's decisions on the other machine (no game access, unit tested). Both machines run an enemy's
// own AI. An enemy's behaviour is the record {state, action id, a, b} its class's setAction (vtable slot 63) stores at
// +0x67a4; the class's handlers turn a new record into a motion. When the owner's record changes, the other machine's
// enemy is given it through setAction, unless its own AI reaches the same action id within kGraceMs. An unchanged
// owner record is never applied again, so the enemy's own action code is never restarted or held.
namespace enemy_action_rule {

constexpr int64_t kGraceMs = 100;
constexpr int kWords = 4;
constexpr int kIdWord = 1;

struct Action {
    int32_t word[kWords] = {};

    int32_t id() const { return word[kIdWord]; }

    bool operator==(const Action& other) const {
        for (int i = 0; i < kWords; ++i) {
            if (word[i] != other.word[i]) return false;
        }
        return true;
    }
    bool operator!=(const Action& other) const { return !(*this == other); }
};

class Cue {
public:
    // The owner's record from a snapshot. A new one (the first one included) becomes pending; a change while one is
    // pending replaces it and keeps the first change's time, so a busy owner is followed without waiting longer.
    void observeOwner(const Action& owner, int64_t nowMs) {
        if (hasOwner_ && owner == owner_) return;
        owner_ = owner;
        hasOwner_ = true;
        if (pending_) return;
        pending_ = true;
        sinceMs_ = nowMs;
    }

    // True when the enemy, whose record is `local`, should now be given owner(). Each change is applied at most once.
    bool due(const Action& local, int64_t nowMs) {
        if (!pending_) return false;
        if (local.id() == owner_.id()) {
            pending_ = false;
            return false;
        }
        if (nowMs - sinceMs_ < kGraceMs) return false;
        pending_ = false;
        return true;
    }

    const Action& owner() const { return owner_; }

private:
    Action owner_;
    bool hasOwner_ = false;
    bool pending_ = false;
    int64_t sinceMs_ = 0;
};

}  // namespace enemy_action_rule
