#pragma once
#include <cstdint>

// Pure rules of an enemy the peer owns and this machine also runs (no game access, unit tested). Its own update moves
// and animates it; what the owner decides reaches it only at the engine's own decision points (enemy_decision.cpp: the
// base family's think step, and the action boundary of the classes sharing its executor), where it may also be put
// back on the owner's pose between two actions.
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

// What the owner's enemy chose, and the pose it chose it from.
struct Decision {
    uint32_t vtable = 0;  // the owner's enemy class: a record only means something to the same class
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

// An action boundary: the executor changed the record's state or action id since its last commit (+0x67b4 holds the
// record as the previous frame ended).
constexpr bool atBoundary(const Action& record, const Action& previous) {
    return record.word[0] != previous.word[0] || record.word[1] != previous.word[1];
}

enum class AtBoundary { Agree, Apply, OtherState };

// At the follower's own action boundary: its AI chose `local`, the owner's newest choice is `owner`. Only the action
// within the same state is taken from the owner; a change of state (death, setup, the class's own phases) stays with
// the class, which reaches it from the replayed hits and its own conditions.
constexpr AtBoundary atOwnBoundary(const Action& local, const Action& owner) {
    if (local.word[0] != owner.word[0]) return AtBoundary::OtherState;
    return local.word[1] == owner.word[1] ? AtBoundary::Agree : AtBoundary::Apply;
}

// The owner's action from its first step. The third word counts an action's steps (live probe: (1,8,0,1) then
// (1,8,1,1) with motions 0x016, 0x01a), and the owner's record is read after its update may have run step 0, so the
// follower starts it the way the engine does, with that word 0.
constexpr Action startOf(const Action& owner) { return {{owner.word[0], owner.word[1], 0, owner.word[3]}}; }

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
