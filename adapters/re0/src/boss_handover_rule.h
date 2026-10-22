#pragma once
#include <algorithm>
#include <cstdint>
#include <span>

#include "boss_wire.h"

// Rules of the boss hand-over (partner_mode=state, docs/RE0_NOTES.md "State mode: boss hand-over"; STATE_PLAN 1.9, RE4E
// 4.1 and 10.5): when the room authority leaves a room with a boss fight in it, the old authority freezes its boss
// members and offers their state (prepare), the new authority confirms it has every member (ready), the old one commits
// and, once the room changes hands, the new authority applies the snapshot and starts each member at step 0. A hand-over
// that cannot finish keeps the members that fit frozen for `kFrozenReleaseMs` (frozen), then resumes them. Pure, no game
// access, unit tested.
namespace boss_handover_rule {

namespace wire = boss_wire;

constexpr uint32_t kPrepareTimeoutMs = 5000;  // a prepare not answered / not committed in this long is cancelled
constexpr uint32_t kCommitWaitMs = 15000;     // after the commit the room must change hands in this long
constexpr uint32_t kFrozenReleaseMs = 5000;   // a frozen hand-over waits this long for the resync, then resumes what fits
constexpr unsigned kTokenSlotShift = 24;      // the issuer's slot + 1 rides above the counter, so a token is never 0
constexpr uint32_t kTokenCounterMask = (1u << kTokenSlotShift) - 1;

// The next token of the machine in `slot`, after `previous` (its own last one, or 0).
constexpr uint32_t nextToken(uint32_t previous, uint8_t slot) {
    const uint32_t counter = ((previous & kTokenCounterMask) + 1) & kTokenCounterMask;
    return ((static_cast<uint32_t>(slot) + 1) << kTokenSlotShift) | counter;
}

// ---- Members against what this machine has -----------------------------------------------------------------------

// The enemy standing in a member's pool slot here, as `enemy_observer::snapshot` reads it.
struct Local {
    bool present = false;
    uint8_t classIndex = 0;
    uint16_t spawnId = 0;
    int16_t hp = 0;
};

// Ok: the same enemy, alive. Dead: the same enemy, already dead here (it died while the old authority was frozen).
// Missing: the slot holds no replicated enemy. Mismatch: another class or spawn stands in the slot.
enum class Fit : uint8_t { Ok, Dead, Missing, Mismatch };

constexpr Fit fitOf(const wire::Member& member, const Local& local) {
    if (!local.present) return Fit::Missing;
    if (local.classIndex != member.classIndex || local.spawnId != member.spawnId) return Fit::Mismatch;
    return local.hp > 0 ? Fit::Ok : Fit::Dead;
}

// A prepare is confirmed when every member is the enemy standing in its slot here (or already dead).
inline bool acceptsPrepare(std::span<const wire::Member> members, std::span<const Local> locals) {
    if (members.size() != locals.size()) return false;
    for (size_t i = 0; i < members.size(); ++i) {
        const Fit fit = fitOf(members[i], locals[i]);
        if (fit == Fit::Missing || fit == Fit::Mismatch) return false;
    }
    return true;
}

// Bit i = member i is alive here.
inline uint8_t aliveMask(std::span<const wire::Member> members, std::span<const Local> locals) {
    uint8_t mask = 0;
    for (size_t i = 0; i < members.size() && i < locals.size(); ++i) {
        if (fitOf(members[i], locals[i]) == Fit::Ok) mask = static_cast<uint8_t>(mask | (1u << i));
    }
    return mask;
}

enum class Verdict : uint8_t { Apply, Frozen };

// What the new authority does when the room changes hands.
struct Plan {
    Verdict verdict = Verdict::Apply;
    uint8_t resume = 0;   // bit i = member i is started again here
    uint8_t alive = 0;    // bit i = member i is alive here
    uint8_t badSlot = 0;  // Frozen: the pool slot of the first member that is missing or another enemy
};

// Every living member is resumed; one that died meanwhile is left dead; one that is missing or is another enemy makes
// the whole hand-over Frozen (the members that fit stay held and a resync is asked); `resume` is also the set to hold.
inline Plan planFlip(std::span<const wire::Member> members, std::span<const Local> locals) {
    Plan plan;
    bool bad = false;
    for (size_t i = 0; i < members.size() && i < locals.size(); ++i) {
        switch (fitOf(members[i], locals[i])) {
            case Fit::Ok:
                plan.resume = static_cast<uint8_t>(plan.resume | (1u << i));
                plan.alive = static_cast<uint8_t>(plan.alive | (1u << i));
                break;
            case Fit::Dead: break;
            case Fit::Missing:
            case Fit::Mismatch:
                if (!bad) plan.badSlot = members[i].slot;
                bad = true;
                break;
        }
    }
    if (bad) plan.verdict = Verdict::Frozen;
    return plan;
}

// The HP a member is resumed with: the snapshot's, or what the stream lowered it to since (damage dealt while the old
// authority was frozen is never given back).
constexpr int16_t resumeHp(int16_t snapshotHp, int16_t localHp) { return std::max<int16_t>(1, std::min(snapshotHp, localHp)); }

// ---- The transaction ---------------------------------------------------------------------------------------------

enum class Role : uint8_t { Old, New };

// Old: Preparing (prepare sent) -> Committed (ready heard, commit sent) -> Away (left the room, hears the outcome).
// New: Prepared (prepare accepted) -> Committed (commit heard) -> Running (the room changed hands; the snapshot is
// applied, or the machine took over alone) or Frozen (it could not be applied).
enum class Stage : uint8_t { Idle, Preparing, Prepared, Committed, Running, Away, Frozen };

// What to do with a transaction that lingers: cancel (Old, tell the peer and start the members again), drop (New) or
// release (New, Frozen: resume the members that fit and carry on as if it had committed alone).
enum class Lapse : uint8_t { None, CancelPrepare, CancelCommit, DropPrepared, DropCommit, ReleaseFrozen };

class Machine {
public:
    Role role() const { return role_; }
    Stage stage() const { return stage_; }
    uint32_t token() const { return token_; }
    uint8_t count() const { return count_; }
    uint8_t alive() const { return alive_; }
    bool active() const { return stage_ != Stage::Idle; }
    // A transaction that has not reached the hand-over yet: only one can be in flight.
    bool inFlight() const { return stage_ == Stage::Preparing || stage_ == Stage::Prepared || stage_ == Stage::Committed; }
    bool owns(uint32_t token) const { return active() && token_ == token; }

    // Old authority: the prepare with `count` members, `alive` of them living, went out.
    void startPrepare(uint32_t token, uint8_t count, uint8_t alive, uint32_t nowMs) { begin(Role::Old, Stage::Preparing, token, count, alive, nowMs); }

    // Old authority: the new one has every member. True when the ready answers this prepare; the commit is next.
    bool onReady(uint32_t token, uint32_t nowMs) {
        if (role_ != Role::Old || stage_ != Stage::Preparing || token != token_) return false;
        enter(Stage::Committed, nowMs);
        return true;
    }

    // Old authority: it left the room after the commit; the new one runs the fight from here.
    void leave(uint32_t nowMs) {
        if (role_ == Role::Old && stage_ == Stage::Committed) enter(Stage::Away, nowMs);
    }

    // New authority: a prepare was accepted.
    void acceptPrepare(uint32_t token, uint8_t count, uint8_t alive, uint32_t nowMs) { begin(Role::New, Stage::Prepared, token, count, alive, nowMs); }

    // New authority: the commit of the accepted prepare.
    bool onCommit(uint32_t token, uint32_t nowMs) {
        if (role_ != Role::New || stage_ != Stage::Prepared || token != token_) return false;
        enter(Stage::Committed, nowMs);
        return true;
    }

    // New authority: the room changed hands. From a prepare or a commit (`token` of it) with the snapshot applied, or
    // alone (a new token, nothing offered).
    void run(uint32_t token, uint8_t count, uint8_t alive, uint32_t nowMs) { begin(Role::New, Stage::Running, token, count, alive, nowMs); }

    // New authority: the members could not be started; the ones that fit stay held until the release.
    void freeze(uint32_t nowMs) {
        if (role_ == Role::New && active()) enter(Stage::Frozen, nowMs);
    }

    // Either authority: the peer cancelled this transaction before the hand-over.
    bool onCancel(uint32_t token) {
        const bool open = (role_ == Role::Old && stage_ == Stage::Preparing) ||
                          (role_ == Role::New && (stage_ == Stage::Prepared || stage_ == Stage::Committed));
        if (!open || token != token_) return false;
        stage_ = Stage::Idle;
        return true;
    }

    // The alive mask changed: the new value is kept. True when it differs from the last one.
    bool noteAlive(uint8_t alive) {
        if (alive == alive_) return false;
        alive_ = alive;
        return true;
    }

    void finish() { stage_ = Stage::Idle; }

    // `peerHere`: the other machine still stands in this transaction's room.
    Lapse lapse(uint32_t nowMs, bool peerHere) const {
        const uint32_t elapsed = nowMs - sinceMs_;
        if (role_ == Role::Old) {
            if (stage_ == Stage::Preparing && (elapsed >= kPrepareTimeoutMs || !peerHere)) return Lapse::CancelPrepare;
            if (stage_ == Stage::Committed && (elapsed >= kCommitWaitMs || !peerHere)) return Lapse::CancelCommit;
            return Lapse::None;
        }
        if (stage_ == Stage::Prepared && elapsed >= kPrepareTimeoutMs) return Lapse::DropPrepared;
        if (stage_ == Stage::Committed && elapsed >= kCommitWaitMs) return Lapse::DropCommit;
        if (stage_ == Stage::Frozen && elapsed >= kFrozenReleaseMs) return Lapse::ReleaseFrozen;
        return Lapse::None;
    }

private:
    void begin(Role role, Stage stage, uint32_t token, uint8_t count, uint8_t alive, uint32_t nowMs) {
        role_ = role;
        token_ = token;
        count_ = count;
        alive_ = alive;
        enter(stage, nowMs);
    }

    void enter(Stage stage, uint32_t nowMs) {
        stage_ = stage;
        sinceMs_ = nowMs;
    }

    Role role_ = Role::Old;
    Stage stage_ = Stage::Idle;
    uint32_t token_ = 0;
    uint32_t sinceMs_ = 0;
    uint8_t count_ = 0;
    uint8_t alive_ = 0;
};

}  // namespace boss_handover_rule
