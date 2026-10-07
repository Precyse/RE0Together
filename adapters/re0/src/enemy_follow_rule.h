#pragma once
#include <cstdint>

// Pure rules of an enemy the peer owns and this machine also runs (no game access, unit tested). Its own update moves
// and animates it; what the owner decides reaches it only at the engine's own decision point (the base family's think
// step, enemy_think.cpp), where it may also be put back on the owner's pose between two actions.
namespace enemy_follow_rule {

constexpr int kActionWords = 4;
constexpr float kRealignDistance = 40.0f;  // a smaller gap at a decision is left to the enemy's own movement
// Ticks this machine ran without an owner snapshot before its own AI decides again (500 ms at 30 fps). Counted in
// ticks, not time, so a world held on both sides (a menu, a door) does not count as silence. The count starts when
// following starts, so the owner's first decisions are waited for, but never longer than this.
constexpr int kOwnerSilentTicks = 15;

// An enemy's behaviour record {state, action id, a, b} at +0x67a4.
struct Action {
    int32_t word[kActionWords] = {};

    bool operator==(const Action& other) const {
        for (int i = 0; i < kActionWords; ++i) {
            if (word[i] != other.word[i]) return false;
        }
        return true;
    }
    bool operator!=(const Action& other) const { return !(*this == other); }
};

// What the owner's think step chose, and the pose it chose it from.
struct Decision {
    Action action;
    float pos[3] = {};
    float quat[4] = {};
};

// Sequence numbers wrap: `seq` is newer than `last` when it is less than half the range ahead.
constexpr bool newer(uint16_t seq, uint16_t last) { return static_cast<int16_t>(static_cast<uint16_t>(seq - last)) > 0; }

// The follower's think step takes the owner's decisions only while the owner is heard from, so a lost owner never
// leaves an enemy waiting for a decision.
constexpr bool thinksForOwner(bool following, int ticksSinceOwner) {
    return following && ticksSinceOwner < kOwnerSilentTicks;
}

constexpr bool realigns(float drift) { return drift > kRealignDistance; }

// Base-family record states (dispatch table 0xd7ab60): 0 sets the enemy up after its spawn, 2 is the think step.
constexpr int32_t kSetupState = 0;
constexpr int32_t kThinkState = 2;

// A record that may stand in for the owner's first decision: never the one-time setup (it would run again) or the
// think state itself (the think step is where it is applied).
constexpr bool seedable(const Action& action) {
    return action.word[0] != kSetupState && action.word[0] != kThinkState;
}

// The owner's newest decision for one enemy that its think step has not applied yet.
class PendingDecision {
public:
    // A decision from the owner, in arrival order; an older or repeated one is ignored.
    void offer(uint16_t seq, const Decision& decision) {
        if (hasSeq_ && !newer(seq, lastSeq_)) return;
        hasSeq_ = true;
        lastSeq_ = seq;
        decision_ = decision;
        pending_ = true;
    }

    // Following starts mid-room: the owner's current record (from a snapshot) stands in until its first decision.
    void seed(const Decision& decision) {
        if (hasSeq_ || pending_) return;
        decision_ = decision;
        pending_ = true;
    }

    // A replayed hit restarted the enemy (its reaction): a decision the owner made before the hit no longer applies.
    void supersede() { pending_ = false; }

    bool take(Decision& out) {
        if (!pending_) return false;
        pending_ = false;
        out = decision_;
        return true;
    }

private:
    Decision decision_;
    bool pending_ = false;
    bool hasSeq_ = false;
    uint16_t lastSeq_ = 0;
};

}  // namespace enemy_follow_rule
