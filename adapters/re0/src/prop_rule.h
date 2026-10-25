#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <vector>

#include "event_rule.h"
#include "stream_clock.h"

// Rules of the push block replication (partner_mode=state, docs/RE0_NOTES.md "State mode: props"). The character that
// pushes a box holds a lease on it (the host arbitrates), streams the box's pose while it moves and the host commits
// the pose it rests at; the other machine shows the stream a render delay late and the commit as its last sample. Pure,
// no game access, unit tested.
namespace prop_rule {

using Vec3 = std::array<float, 3>;

constexpr uint32_t kMoveIntervalMs = 100;     // a moving box is sent at 10 Hz
constexpr uint32_t kKeepAliveMs = 1000;       // a holder that pushes without moving still says so this often
constexpr uint32_t kLeaseIdleMs = 4000;       // no request or move for this long: the lease lapses
constexpr uint32_t kRequestTimeoutMs = 2000;  // a pusher stops waiting for the host's answer
constexpr float kMoveEpsilon = 0.5f;          // game units the box must have moved to count as moving
constexpr float kYawEpsilon = 0.01f;          // radians
constexpr size_t kMaxLeases = 16;
constexpr size_t kMaxJournal = 32;
constexpr size_t kMaxSamples = 16;

// One obstacle of a room: the scene id and the row of the engine's obstacle table (a room script's p2 0..4).
struct Key {
    uint16_t scene;
    uint8_t unit;
    bool operator==(const Key&) const = default;
};

struct Pose {
    Vec3 pos{};
    float yaw = 0.0f;
};

// The box's yaw from its rotation quaternion (x, y, z, w; exact for a rotation about the vertical axis, a stable
// function of the rotation otherwise) and the quaternion of a yaw.
inline float yawOf(const float (&quat)[4]) { return 2.0f * std::atan2(quat[1], quat[3]); }

inline void quatOf(float yaw, float (&out)[4]) {
    out[0] = 0.0f;
    out[1] = std::sin(yaw * 0.5f);
    out[2] = 0.0f;
    out[3] = std::cos(yaw * 0.5f);
}

inline float yawGap(float a, float b) { return std::fabs(std::remainder(a - b, 2.0f * std::numbers::pi_v<float>)); }

inline float distance(const Vec3& a, const Vec3& b) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// The box is somewhere else than `b` says.
inline bool differs(const Pose& a, const Pose& b) {
    return distance(a.pos, b.pos) >= kMoveEpsilon || yawGap(a.yaw, b.yaw) >= kYawEpsilon;
}

// ---- Leases ------------------------------------------------------------------------------------------------------

struct Held {
    Key key;
    uint8_t holder;
    uint32_t serial;
    uint32_t activeMs;  // the last request, keep-alive or move
    bool hasPose;
    uint32_t poseMs;  // the holder's clock at `pose`
    Pose pose;
};

struct Decision {
    bool granted;
    uint32_t serial;
    uint8_t holder;  // who holds the box after this decision
};

enum class MoveResult : uint8_t { Accepted, NotHolder, Stale };

// Who holds which box. The host decides (`request`); every machine keeps the table its messages describe (`adopt`) so
// it knows whose moves to take. A lease lapses `kLeaseIdleMs` after its last sign of life.
class LeaseTable {
public:
    // Host: a pusher asks for a box. The first asker holds it; the holder asking again keeps it (same serial); a lapsed
    // lease is up for grabs.
    Decision request(const Key& key, uint8_t slot, uint32_t nowMs) {
        Held* held = find(key);
        if (held && !lapsed(*held, nowMs)) {
            if (held->holder != slot) return {false, held->serial, held->holder};
            held->activeMs = nowMs;
            return {true, held->serial, slot};
        }
        const Held fresh{key, slot, ++serial_, nowMs, false, 0, {}};
        if (held) *held = fresh; else insert(fresh);
        return {true, fresh.serial, slot};
    }

    // Any machine: the host said `holder` holds `key` under `serial`.
    void adopt(const Key& key, uint8_t holder, uint32_t serial, uint32_t nowMs) {
        const Held fresh{key, holder, serial, nowMs, false, 0, {}};
        if (Held* held = find(key)) *held = fresh; else insert(fresh);
    }

    // The holder is still pushing.
    void touch(const Key& key, uint8_t slot, uint32_t nowMs) {
        if (Held* held = find(key); held && held->holder == slot) held->activeMs = nowMs;
    }

    // A move from `slot`: taken only from the holder, and only when newer than the last one taken.
    MoveResult acceptMove(const Key& key, uint8_t slot, uint32_t senderMs, const Pose& pose, uint32_t nowMs) {
        Held* held = find(key);
        if (!held || held->holder != slot || lapsed(*held, nowMs)) return MoveResult::NotHolder;
        if (held->hasPose && !stream_clock::after(senderMs, held->poseMs)) return MoveResult::Stale;
        held->hasPose = true;
        held->poseMs = senderMs;
        held->pose = pose;
        held->activeMs = nowMs;
        return MoveResult::Accepted;
    }

    // `slot` lets go: its lease, with the last pose it sent, or nothing when it did not hold the box.
    std::optional<Held> release(const Key& key, uint8_t slot) {
        Held* held = find(key);
        if (!held || held->holder != slot) return std::nullopt;
        return take(held);
    }

    // The box is committed: whatever lease was on it is over.
    std::optional<Held> drop(const Key& key) {
        Held* held = find(key);
        return held ? std::optional<Held>(take(held)) : std::nullopt;
    }

    // Removes and returns the leases that lapsed.
    std::vector<Held> expire(uint32_t nowMs) {
        std::vector<Held> gone;
        for (size_t index = leases_.size(); index-- > 0;) {
            if (!lapsed(leases_[index], nowMs)) continue;
            gone.push_back(leases_[index]);
            leases_.erase(leases_.begin() + static_cast<std::ptrdiff_t>(index));
        }
        return gone;
    }

    std::optional<uint8_t> holderOf(const Key& key, uint32_t nowMs) const {
        for (const Held& held : leases_) {
            if (held.key == key) return lapsed(held, nowMs) ? std::nullopt : std::optional<uint8_t>(held.holder);
        }
        return std::nullopt;
    }

    void clear() { leases_.clear(); }

    size_t size() const { return leases_.size(); }

private:
    static bool lapsed(const Held& held, uint32_t nowMs) { return nowMs - held.activeMs >= kLeaseIdleMs; }

    Held* find(const Key& key) {
        for (Held& held : leases_) {
            if (held.key == key) return &held;
        }
        return nullptr;
    }

    Held take(Held* held) {
        const Held copy = *held;
        leases_.erase(leases_.begin() + (held - leases_.data()));
        return copy;
    }

    void insert(const Held& held) {
        if (leases_.size() >= kMaxLeases) leases_.erase(leases_.begin());
        leases_.push_back(held);
    }

    std::vector<Held> leases_;
    uint32_t serial_ = 0;
};

// ---- Commits -----------------------------------------------------------------------------------------------------

struct Record {
    Key key;
    uint32_t serial;
    uint8_t committer;
    Pose pose;
};

// The host's journal of finished pushes (every machine keeps what it was told): the newest commit per box. It answers
// who last pushed a box, which decides who fires the box's zone trigger, and is what a joiner is told.
class Journal {
public:
    void note(const Record& record) {
        for (Record& known : records_) {
            if (!(known.key == record.key)) continue;
            if (!stream_clock::after(record.serial, known.serial) && record.serial != known.serial) return;
            known = record;
            return;
        }
        if (records_.size() >= kMaxJournal) records_.erase(records_.begin());
        records_.push_back(record);
    }

    std::optional<uint8_t> lastCommitter(const Key& key) const {
        for (const Record& record : records_) {
            if (record.key == key) return record.committer;
        }
        return std::nullopt;
    }

    // A room loaded fresh: its boxes start from the flags' layout again, so what was pushed in it before is history.
    void forgetScene(uint16_t scene) {
        records_.erase(std::remove_if(records_.begin(), records_.end(), [&](const Record& r) { return r.key.scene == scene; }),
                       records_.end());
    }

    void clear() { records_.clear(); }

    const std::vector<Record>& records() const { return records_; }

private:
    std::vector<Record> records_;
};

// Who fires a box's zone trigger: the holder, else the last committer; with neither the room's authority does.
inline event_rule::PropOwner ownerOf(std::optional<uint8_t> slot, uint8_t localSlot) {
    if (!slot) return event_rule::PropOwner::Unknown;
    return *slot == localSlot ? event_rule::PropOwner::Mine : event_rule::PropOwner::Theirs;
}

// ---- The pusher --------------------------------------------------------------------------------------------------

// What the local character's push shows on one game tick.
struct Observation {
    bool pushing = false;  // think group 0 state 9 with a box that is one of the room's obstacle units
    Key key{};
    Pose pose;             // the box's
    uint32_t nowMs = 0;
};

// What the tick must do. `request`: ask for the lease. `move`: send `pose`. `end`: the push is over (read the box's
// final pose, send it, let go); `cancel`: the push ended before any answer came, let go without a pose.
struct Step {
    Key key{};
    bool request = false;
    bool move = false;
    bool end = false;
    bool cancel = false;
    bool timedOut = false;
    Pose pose;
};

// The local push from its first frame to its last: asks for the lease, waits for the answer, then streams while it
// holds it. A denied push (the other machine holds the box) says nothing more.
class PushTracker {
public:
    enum class Phase : uint8_t { Idle, Asked, Granted, Denied };

    Step observe(const Observation& now) {
        Step step;
        if (phase_ == Phase::Idle) {
            if (!now.pushing) return step;
            begin(now);
            step.key = key_;
            step.request = true;
            return step;
        }
        step.key = key_;
        if (!now.pushing || !(now.key == key_)) {
            step.end = phase_ == Phase::Granted;
            step.cancel = phase_ == Phase::Asked;
            phase_ = Phase::Idle;
            return step;
        }
        if (phase_ == Phase::Asked && now.nowMs - askedMs_ >= kRequestTimeoutMs) {
            phase_ = Phase::Denied;
            step.timedOut = true;
        } else if (phase_ == Phase::Granted && due(now)) {
            step.move = true;
            step.pose = now.pose;
            sentMs_ = now.nowMs;
            sent_ = now.pose;
            sentOnce_ = true;
        }
        return step;
    }

    // The host's answer to the request for `key`.
    void answered(const Key& key, bool granted) {
        if (phase_ != Phase::Asked || !(key == key_)) return;
        phase_ = granted ? Phase::Granted : Phase::Denied;
    }

    Phase phase() const { return phase_; }

    void reset() { phase_ = Phase::Idle; }

private:
    void begin(const Observation& now) {
        key_ = now.key;
        phase_ = Phase::Asked;
        askedMs_ = now.nowMs;
        sentOnce_ = false;
    }

    // The first pose goes at once; then 10 Hz while the box moves, a keep-alive otherwise.
    bool due(const Observation& now) const {
        if (!sentOnce_) return true;
        const uint32_t since = now.nowMs - sentMs_;
        return since >= (differs(now.pose, sent_) ? kMoveIntervalMs : kKeepAliveMs);
    }

    Phase phase_ = Phase::Idle;
    Key key_{};
    uint32_t askedMs_ = 0;
    uint32_t sentMs_ = 0;
    bool sentOnce_ = false;
    Pose sent_;
};

// ---- The other machine -------------------------------------------------------------------------------------------

struct Sample {
    uint32_t senderMs;
    Pose pose;
    bool final;  // the host's commit: the pose the push rests at
};

struct Shown {
    bool valid = false;
    bool finished = false;  // the final pose is being shown: the track is done
    Pose pose;
};

// One box's pose stream behind the pusher's shared clock, shown the clock's render delay late like everything else
// of that peer: position interpolated, nothing extrapolated, the commit as the last sample.
class Track {
public:
    void clear() {
        samples_.clear();
        immediate_.reset();
        hasGeneration_ = false;
    }

    bool empty() const { return samples_.empty() && !immediate_; }

    void push(const stream_clock::StreamClock& clock, const Sample& sample) {
        followClock(clock);
        if (!samples_.empty() && samples_.back().final) return;
        const auto at = std::find_if(samples_.begin(), samples_.end(), [&](const Sample& held) {
            return !stream_clock::after(sample.senderMs, held.senderMs);
        });
        if (at != samples_.end() && at->senderMs == sample.senderMs) return;
        samples_.insert(at, sample);
        if (samples_.size() > kMaxSamples) samples_.erase(samples_.begin());
    }

    // The host committed `exact`: it follows the last sample, or is shown at once when there is no stream to follow.
    void finish(const stream_clock::StreamClock* clock, const Pose& exact) {
        if (clock) followClock(*clock);
        if (!clock || samples_.empty()) {
            immediate_ = exact;
            return;
        }
        if (samples_.back().final) samples_.pop_back();
        samples_.push_back({samples_.back().senderMs + 1, exact, true});
    }

    Shown render(const stream_clock::StreamClock& clock, uint32_t nowMs) {
        if (immediate_) return {true, true, *immediate_};
        followClock(clock);
        if (!clock.ready() || samples_.empty()) return {};
        const uint32_t target = clock.target(nowMs);
        size_t reached = 0;
        while (reached < samples_.size() && !stream_clock::after(samples_[reached].senderMs, target)) ++reached;
        if (reached == 0) return {};
        samples_.erase(samples_.begin(), samples_.begin() + static_cast<std::ptrdiff_t>(reached - 1));  // the last one reached stays as the anchor
        if (samples_.size() == 1) return {true, samples_[0].final, samples_[0].pose};
        return {true, false, interpolate(samples_[0], samples_[1], target)};
    }

private:
    static Pose interpolate(const Sample& a, const Sample& b, uint32_t target) {
        const uint32_t span = b.senderMs - a.senderMs;
        const float t = span == 0 ? 0.0f : std::clamp(static_cast<float>(target - a.senderMs) / static_cast<float>(span), 0.0f, 1.0f);
        Pose out;
        for (size_t axis = 0; axis < out.pos.size(); ++axis) out.pos[axis] = a.pose.pos[axis] + (b.pose.pos[axis] - a.pose.pos[axis]) * t;
        out.yaw = a.pose.yaw + std::remainder(b.pose.yaw - a.pose.yaw, 2.0f * std::numbers::pi_v<float>) * t;
        return out;
    }

    // The shared clock restarted: what was buffered is on the old mapping.
    void followClock(const stream_clock::StreamClock& clock) {
        if (hasGeneration_ && generation_ != clock.generation()) samples_.clear();
        generation_ = clock.generation();
        hasGeneration_ = true;
    }

    std::vector<Sample> samples_;  // by the pusher's time
    std::optional<Pose> immediate_;
    uint32_t generation_ = 0;
    bool hasGeneration_ = false;
};

}  // namespace prop_rule
