#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "enemy_observer_rule.h"
#include "enemy_observer_wire.h"

// Pure rules of the enemy drift trace (unit tested, docs/RE0_NOTES.md "Enemy drift trace"): when an observed enemy's
// own state has stayed away from the sample written into it, when that drift is over, how many log lines may be
// written, the counters of the periodic summary and the per-shot ledger of the shooter.
namespace enemy_drift_rule {

// ---- Fields ------------------------------------------------------------------------------------------------------

enum class Field : uint8_t { Hp, Alive, ShotSkip, RecordState, Motion };
constexpr size_t kFieldCount = 5;

constexpr const char* name(Field field) {
    switch (field) {
        case Field::Hp: return "hp";
        case Field::Alive: return "alive";
        case Field::ShotSkip: return "shot-skip bits";
        case Field::RecordState: return "record state";
        case Field::Motion: return "motion";
    }
    return "?";
}

constexpr uint32_t kSettleMs = 300;        // a mismatch counts as drift only after it has lasted this long
constexpr uint32_t kMotionSettleMs = 600;  // motion blends lag the stream longer than the discrete fields do

constexpr uint32_t settleMs(Field field) { return field == Field::Motion ? kMotionSettleMs : kSettleMs; }

// The fields the hit test depends on: put back at once, right after the enemy's move.
constexpr bool corrected(Field field) { return field == Field::Hp || field == Field::Alive || field == Field::ShotSkip; }

// One value per field, from the sample that was written or from the enemy as the move left it.
struct Values {
    int32_t hp = 0;
    int32_t alive = 0;
    int32_t shotSkip = 0;
    int32_t recordState = 0;
    int32_t motion = 0;
    bool recordKnown = true;  // false while the simulator's enemy is still in its setup state: that record is not written

    constexpr int32_t of(Field field) const {
        switch (field) {
            case Field::Hp: return hp;
            case Field::Alive: return alive;
            case Field::ShotSkip: return shotSkip;
            case Field::RecordState: return recordState;
            case Field::Motion: return motion;
        }
        return 0;
    }
    constexpr bool compares(Field field) const { return field != Field::RecordState || recordKnown; }
};

// What the written sample says the enemy shows (a living enemy shows its shot-skip bits clear).
inline Values expectedOf(const enemy_observer_rule::Sample& sample) {
    const bool alive = (sample.flags & enemy_observer_wire::kFlagAlive) != 0 && sample.hp > 0;
    Values values;
    values.hp = sample.hp;
    values.alive = alive;
    values.shotSkip = static_cast<int32_t>(enemy_observer_rule::shownFlags6a0c(sample.flags6a0c, alive) & enemy_observer_rule::kShotSkipBits);
    values.recordState = sample.record[0];
    values.motion = sample.motion0;
    values.recordKnown = enemy_observer_rule::recordApplies(sample.record);
    return values;
}

// What the enemy holds after its move, from enemy_observer_game::read.
inline Values actualOf(const enemy_observer_wire::PoseEntry& read) {
    Values values;
    values.hp = read.hp;
    values.alive = read.hp > 0;
    values.shotSkip = static_cast<int32_t>(read.flags6a0c & enemy_observer_rule::kShotSkipBits);
    values.recordState = read.record[0];
    values.motion = read.motion0;
    return values;
}

// ---- Trackers ----------------------------------------------------------------------------------------------------

enum class Event : uint8_t { None, Onset, Cleared };

// One field of one enemy. Onset once, after the mismatch has lasted the settle time without a match in between;
// Cleared at the first match after an onset; nothing in between, however long the same drift goes on.
class Tracker {
public:
    explicit Tracker(uint32_t settleMs = kSettleMs) : settleMs_(settleMs) {}

    Event observe(int32_t expected, int32_t actual, uint32_t nowMs) {
        if (expected == actual) {
            const bool wasReported = reported_;
            lastDriftMs_ = mismatching_ ? nowMs - sinceMs_ : 0;
            mismatching_ = false;
            reported_ = false;
            return wasReported ? Event::Cleared : Event::None;
        }
        if (!mismatching_) {
            mismatching_ = true;
            sinceMs_ = nowMs;
        }
        if (reported_ || nowMs - sinceMs_ < settleMs_) return Event::None;
        reported_ = true;
        return Event::Onset;
    }

    // How long the current mismatch has lasted; after a Cleared, how long the drift that ended had lasted.
    uint32_t driftMs(uint32_t nowMs) const { return mismatching_ ? nowMs - sinceMs_ : lastDriftMs_; }

private:
    uint32_t settleMs_;
    bool mismatching_ = false;
    bool reported_ = false;
    uint32_t sinceMs_ = 0;
    uint32_t lastDriftMs_ = 0;
};

// All five fields of one pool slot's enemy.
class SlotTrackers {
public:
    SlotTrackers() {
        for (size_t index = 0; index < kFieldCount; ++index) trackers_[index] = Tracker(settleMs(static_cast<Field>(index)));
    }

    Event observe(Field field, int32_t expected, int32_t actual, uint32_t nowMs) {
        return trackers_[static_cast<size_t>(field)].observe(expected, actual, nowMs);
    }
    uint32_t driftMs(Field field, uint32_t nowMs) const { return trackers_[static_cast<size_t>(field)].driftMs(nowMs); }

private:
    std::array<Tracker, kFieldCount> trackers_;
};

// ---- Log budget and summary --------------------------------------------------------------------------------------

constexpr uint32_t kLinesPerWindow = 8;
constexpr uint32_t kWindowMs = 10000;
constexpr uint32_t kSummaryMs = 30000;

// At most kLinesPerWindow lines per kWindowMs; the caller counts the refused ones as suppressed.
class LineBudget {
public:
    bool allow(uint32_t nowMs) {
        if (!started_ || nowMs - windowStartMs_ >= kWindowMs) {
            started_ = true;
            windowStartMs_ = nowMs;
            used_ = 0;
        }
        if (used_ >= kLinesPerWindow) return false;
        ++used_;
        return true;
    }

private:
    bool started_ = false;
    uint32_t windowStartMs_ = 0;
    uint32_t used_ = 0;
};

// The counters of the periodic summary line. due() is true every kSummaryMs, counted from the first count of a window,
// and never while nothing was counted; take() hands the counters out and starts a new window.
struct Summary {
    std::array<uint32_t, kFieldCount> onsets{};
    uint32_t corrections = 0;
    uint32_t shotsChecked = 0;
    uint32_t shotAnomalies = 0;
    uint32_t suppressed = 0;

    bool any() const {
        return corrections != 0 || shotsChecked != 0 || shotAnomalies != 0 || suppressed != 0 ||
               std::any_of(onsets.begin(), onsets.end(), [](uint32_t count) { return count != 0; });
    }

    bool due(uint32_t nowMs) {
        if (!any()) return false;
        if (!started_) {
            started_ = true;
            startMs_ = nowMs;
        }
        return nowMs - startMs_ >= kSummaryMs;
    }

    Summary take() {
        const Summary taken = *this;
        *this = Summary{};
        return taken;
    }

private:
    bool started_ = false;
    uint32_t startMs_ = 0;
};

// ---- Shots -------------------------------------------------------------------------------------------------------

constexpr uint32_t kHitWindowMs = 250;      // a hit routed this soon after a shot belongs to it
constexpr uint32_t kAnswerWindowMs = 1000;  // the shot is judged when this has passed since the trigger pull
constexpr size_t kMaxOpenShots = 16;        // more than this in one answer window is not a rate a gun reaches

// The living observed enemies whose shot-skip bits were set at the trigger pull (the first of them is named).
struct SkipSighting {
    uint32_t count = 0;
    uint8_t slot = 0;
    uint8_t classIndex = 0;
    uint32_t flags6a0c = 0;
};

struct Shot {
    uint32_t firedMs = 0;
    SkipSighting skipping;
    uint32_t routed = 0;
    uint32_t applied = 0;
    uint32_t refused = 0;

    bool anomalous() const { return skipping.count != 0 || applied < routed; }
};

// The open shots of the local player. Hits are attributed to the newest shot inside kHitWindowMs, answers (HIT_APPLIED)
// to the oldest shot still waiting for one; a shot is closed, and returned by expire, once its answer window is over.
class ShotLedger {
public:
    void fired(uint32_t nowMs, const SkipSighting& skipping) {
        if (open_.size() == kMaxOpenShots) open_.erase(open_.begin());
        Shot shot;
        shot.firedMs = nowMs;
        shot.skipping = skipping;
        open_.push_back(shot);
    }

    // A hit of the newest recent shot went to its simulator; `answered` when this machine is the simulator and the hit
    // is already applied.
    void routed(uint32_t nowMs, bool answered) {
        if (Shot* shot = recent(nowMs)) {
            ++shot->routed;
            if (answered) ++shot->applied;
        }
    }

    void refused(uint32_t nowMs) {
        if (Shot* shot = recent(nowMs)) ++shot->refused;
    }

    void applied() {
        const auto waiting = std::find_if(open_.begin(), open_.end(), [](const Shot& shot) { return shot.applied < shot.routed; });
        if (waiting != open_.end()) ++waiting->applied;
    }

    void expire(uint32_t nowMs, std::vector<Shot>& closed) {
        const auto firstOpen = std::find_if(open_.begin(), open_.end(), [&](const Shot& shot) { return nowMs - shot.firedMs < kAnswerWindowMs; });
        closed.insert(closed.end(), open_.begin(), firstOpen);
        open_.erase(open_.begin(), firstOpen);
    }

private:
    Shot* recent(uint32_t nowMs) {
        if (open_.empty() || nowMs - open_.back().firedMs > kHitWindowMs) return nullptr;
        return &open_.back();
    }

    std::vector<Shot> open_;
};

}  // namespace enemy_drift_rule
