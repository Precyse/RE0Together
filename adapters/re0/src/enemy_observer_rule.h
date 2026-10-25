#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "enemy_follow_rule.h"
#include "enemy_observer_wire.h"
#include "enemy_policy.h"
#include "enemy_spawn.h"
#include "pose_timeline.h"
#include "position_blend.h"
#include "stream_clock.h"

// Pure rules of the enemy observers (unit tested): which enemies the engine's own update is replaced for, which stream
// entries are applied to which local enemy, and the per-enemy timeline of the simulator's samples.
namespace enemy_observer_rule {

using enemy_observer_wire::Header;
using enemy_observer_wire::MotionEntry;
using enemy_observer_wire::PoseEntry;
using enemy_policy::Policy;

constexpr int32_t kSetupState = 0;  // record word 0 of an enemy that is still constructing; the class's setup runs it
constexpr size_t kMaxSamples = 32;  // 20 Hz: well over a second of samples

// ---- Scope -------------------------------------------------------------------------------------------------------

// Construction runs natively: the scope applies only once the enemy's record has left the setup state. The gate follows
// the record, so an enemy created again in the same place closes it again until its setup is over.
class ConstructionGate {
public:
    void note(int32_t recordState) { open_ = recordState != kSetupState; }
    bool open() const { return open_; }

private:
    bool open_ = false;
};

// This machine shows the enemy from the stream instead of simulating it: the other machine runs the room and the class
// is an Observer class. Decisions, follow-start alignment and replayed hits are skipped for such an enemy.
constexpr bool observed(Policy policy, bool peerRunsRoom) { return peerRunsRoom && policy == Policy::Observer; }

// The engine's update is replaced by the CharPause path for an observed enemy right now: its construction is over and
// the simulator has not released it (`removed`: the class's own code takes the unit out).
constexpr bool scoped(bool isObserved, bool constructed, bool released) { return isObserved && constructed && !released; }

// ---- Which stream entries apply ----------------------------------------------------------------------------------

enum class Refusal : uint8_t { None, Sender, Scene, Epoch };

constexpr const char* name(Refusal refusal) {
    switch (refusal) {
        case Refusal::Sender: return "sender";
        case Refusal::Scene: return "scene";
        case Refusal::Epoch: return "epoch";
        case Refusal::None: break;
    }
    return "none";
}

// A batch counts only from the slot that runs its room, for the scene loaded here, under the table's current epoch.
constexpr Refusal refusalOf(const Header& header, uint8_t senderSlot, uint8_t holderSlot, uint32_t holderEpoch,
                            uint16_t loadedScene) {
    if (senderSlot != holderSlot) return Refusal::Sender;
    if (header.scene != loadedScene) return Refusal::Scene;
    if (header.roomEpoch != holderEpoch) return Refusal::Epoch;
    return Refusal::None;
}

// What this machine knows about the enemy standing in a pool slot.
struct LocalEnemy {
    uint8_t classIndex = 0;
    uint16_t spawnId = 0;
    uint16_t record = 0;  // the spawn record it was created from: the engine's identity of the enemy on both machines

    bool operator==(const LocalEnemy&) const = default;
};

constexpr int kNoPoolSlot = -1;

// The pool slot of the enemy created from spawn record `record`, among the spawns of this machine's pool enemies (one per
// pool slot, nothing for an empty slot): the stream entry's own pool slot plays no part, the two machines' pools need
// not agree.
template <size_t Slots>
constexpr int slotOfRecord(const std::array<std::optional<enemy_spawn::Origin>, Slots>& origins, uint16_t record) {
    for (size_t slot = 0; slot < Slots; ++slot) {
        if (origins[slot] && origins[slot]->record == record) return static_cast<int>(slot);
    }
    return kNoPoolSlot;
}

// The earlier pool slot whose enemy was created from the same spawn record as the one in `slot` (the same enemy built
// twice), or kNoPoolSlot. The stream binds the earlier one (slotOfRecord); the later one gets no data and stays held.
template <size_t Slots>
constexpr int duplicateOf(const std::array<std::optional<enemy_spawn::Origin>, Slots>& origins, size_t slot) {
    if (slot >= Slots || !origins[slot]) return kNoPoolSlot;
    const int first = slotOfRecord(origins, origins[slot]->record);
    return first != kNoPoolSlot && static_cast<size_t>(first) < slot ? first : kNoPoolSlot;
}

// A stream entry is applied to the local enemy only when both were created from the same spawn record, of the same class
// and spawn id. The pool slots of the two machines need not agree.
constexpr bool bound(const LocalEnemy& local, const PoseEntry& entry) {
    return local.classIndex == entry.classIndex && local.spawnId == entry.spawnId && local.record == entry.spawnRecord;
}

// The record words are written only once the simulator's enemy has left its setup state.
constexpr bool recordApplies(const int32_t (&record)[4]) { return record[0] != kSetupState; }

enum class Life : uint8_t { Alive, Dead, Gone };

// What a shown sample says about the enemy's life: `Dead` plays out from the stream, `Gone` hands the unit back to the
// class's own code.
constexpr Life lifeOf(uint8_t flags) {
    if ((flags & enemy_observer_wire::kFlagRemoved) != 0) return Life::Gone;
    return (flags & enemy_observer_wire::kFlagTerminal) != 0 ? Life::Dead : Life::Alive;
}

// ---- Authority change --------------------------------------------------------------------------------------------

constexpr uint32_t kHandOverWaitMs = 2000;  // the old simulator is gone but nobody has claimed the room yet: hold this long
constexpr uint32_t kAdoptMaxAgeMs = 3000;   // a hand-over adopts the newest received sample of an enemy only when it is this fresh

// How the loaded room stands for this machine: whose stream it shows, or that it runs the room itself.
struct Role {
    bool observing = false;
    bool simulating = false;
    uint16_t scene = 0xffff;
    uint8_t holderSlot = 0;
    uint32_t epoch = 0;

    bool operator==(const Role&) const = default;
};

enum class RoleChange : uint8_t {
    None,      // nothing changed
    Restart,   // another stream or room: every slot starts over
    Wait,      // the room we were observing lost its simulator and nobody holds it yet: keep the enemies held
    HandOver,  // this machine now runs the room it was observing: its enemies carry on from where they were shown
};

// What a role change means. `waitedMs` is how long the room has been waiting for a simulator (0 when it has not).
constexpr RoleChange roleChange(const Role& before, const Role& now, uint32_t waitedMs) {
    if (before == now) return RoleChange::None;
    if (before.observing && now.scene == before.scene) {
        if (now.simulating) return RoleChange::HandOver;
        if (!now.observing && waitedMs < kHandOverWaitMs) return RoleChange::Wait;
    }
    return RoleChange::Restart;
}

// True when a sample that arrived at `arrivalMs` is fresh enough for a hand-over to take over (wrap-safe).
constexpr bool adoptable(uint32_t nowMs, uint32_t arrivalMs) { return nowMs - arrivalMs <= kAdoptMaxAgeMs; }

enum class Start : uint8_t {
    Corpse,  // dead: its own update plays it out from the record the stream left
    Think,   // in its think (or setup) state: its own think picks the next action
    Resume,  // in an action: the class's setAction starts that action again at step 0
};

// How an enemy that was shown from the stream carries on when this machine simulates it. Slot 41 never resumes with a
// motion its state machine did not pick, so an action is started again from its first step (enemy_follow_rule::startOf).
constexpr Start handOverStart(int16_t hp, const enemy_follow_rule::Action& record) {
    if (hp <= 0) return Start::Corpse;
    return enemy_follow_rule::seedable(record) ? Start::Resume : Start::Think;
}

// ---- Hits --------------------------------------------------------------------------------------------------------

// The machine that simulates an enemy right now (the lease holder, else the room authority), as this machine sees it.
struct Simulator {
    bool known = false;  // false while the room is unowned or there is no linked session
    bool here = false;
    uint8_t slot = 0;
};

enum class Route : uint8_t { Apply, Send, Local };

// Where a hit by this machine's player goes: run here when this machine simulates the enemy, otherwise to the
// simulator as HIT_REQUEST, and on this machine alone while nobody simulates it.
constexpr Route routeOfOwnHit(const Simulator& simulator) {
    if (simulator.here) return Route::Apply;
    return simulator.known ? Route::Send : Route::Local;
}

enum class Fate : uint8_t { Apply, Forward, Drop };

// A HIT_REQUEST that reaches this machine: applied when it simulates the enemy, otherwise sent on once to the machine
// that does (the sender's tables were a step behind a hand-over or a lease); one that was already sent on is dropped.
constexpr Fate fateOfRequest(const Simulator& simulator, bool alreadyForwarded) {
    if (simulator.here) return Fate::Apply;
    return simulator.known && !alreadyForwarded ? Fate::Forward : Fate::Drop;
}

// ---- Creation on demand ------------------------------------------------------------------------------------------

constexpr size_t kMaxCreatesPerFrame = 4;
constexpr uint32_t kCreateSlotIntervalMs = 1000;

// A living Observer-class enemy of the stream whose spawn record no enemy here was created from (a script spawn this
// machine has not run, a late arrival) is created natively from that record, whichever pool slot the record's enemy
// stands in on the other machine. A boss is never built this way: its room's script builds it on both machines.
// A living enemy of an Observer class.
inline bool livingObserver(const PoseEntry& entry) {
    constexpr uint8_t kLifeFlags = enemy_observer_wire::kFlagAlive | enemy_observer_wire::kFlagTerminal | enemy_observer_wire::kFlagRemoved;
    return (entry.flags & kLifeFlags) == enemy_observer_wire::kFlagAlive && enemy_policy::kClasses[entry.classIndex].policy == Policy::Observer;
}

inline bool createWanted(bool recordHere, const PoseEntry& entry) {
    return !recordHere && livingObserver(entry) && !enemy_policy::bossClass(entry.classIndex);
}

// At most kMaxCreatesPerFrame creations a frame and one a second per slot.
class CreatePacer {
public:
    void beginFrame() { used_ = 0; }

    bool allow(size_t slot, uint32_t nowMs) {
        if (used_ >= kMaxCreatesPerFrame || slot >= lastMs_.size()) return false;
        if (seen_[slot] && nowMs - lastMs_[slot] < kCreateSlotIntervalMs) return false;
        seen_[slot] = true;
        lastMs_[slot] = nowMs;
        ++used_;
        return true;
    }

    void clear() { *this = CreatePacer{}; }

private:
    static constexpr size_t kSlots = game::kEnemyPoolSlots;
    std::array<uint32_t, kSlots> lastMs_{};
    std::array<bool, kSlots> seen_{};
    size_t used_ = 0;
};

// The enemies the simulator streams alive that have no enemy here (RE1-Coop's parity watch): each record is reported once
// when it has been missing kMissingReportMs without a gap of kMissingGapMs in its sightings; the create keeps retrying.
constexpr uint32_t kMissingReportMs = 3000;
constexpr uint32_t kMissingGapMs = 1000;

class MissingWatch {
public:
    // An alive entry of `record` with no enemy here. True once, when the record has been missing kMissingReportMs.
    bool missing(uint16_t record, uint32_t nowMs) {
        if (record >= since_.size()) return false;
        Seen& seen = since_[record];
        if (!seen.known || nowMs - seen.lastMs > kMissingGapMs) seen = {true, false, nowMs, nowMs};
        seen.lastMs = nowMs;
        if (seen.reported || nowMs - seen.firstMs < kMissingReportMs) return false;
        seen.reported = true;
        return true;
    }

    // How long `record` has been missing (0 when it is not).
    uint32_t missingForMs(uint16_t record) const {
        return record < since_.size() && since_[record].known ? since_[record].lastMs - since_[record].firstMs : 0;
    }

    // `record` has an enemy here: its watch starts over.
    void present(uint16_t record) {
        if (record < since_.size()) since_[record] = {};
    }

    void clear() { since_.fill({}); }

private:
    struct Seen {
        bool known = false;
        bool reported = false;
        uint32_t firstMs = 0;
        uint32_t lastMs = 0;
    };
    std::array<Seen, game::kSpawnRecordCount> since_{};
};

// ---- Sequences ---------------------------------------------------------------------------------------------------

constexpr uint32_t kSeqHalf = 0x8000;
constexpr uint32_t kSeqSpan = 0x10000;

// The 32-bit request number whose low 16 bits are `low`, nearest to `reference` (the newest one seen on the stream).
constexpr uint32_t extendSeq(uint32_t reference, uint16_t low) {
    uint32_t candidate = (reference & ~(kSeqSpan - 1)) | low;
    const int32_t ahead = static_cast<int32_t>(candidate - reference);
    if (ahead > static_cast<int32_t>(kSeqHalf)) candidate -= kSeqSpan;
    if (ahead < -static_cast<int32_t>(kSeqHalf)) candidate += kSeqSpan;
    return candidate;
}

// ---- Samples -----------------------------------------------------------------------------------------------------

// One decoded ENEMY_POSE entry on the simulator's clock.
struct Sample {
    uint32_t senderMs = 0;
    uint32_t arrivalMs = 0;
    uint32_t seq = 0;  // the batch number: the entries of one batch are different slots
    uint8_t flags = 0;
    uint8_t target = enemy_observer_wire::kNoTarget;
    uint16_t spawnRecord = 0;
    int16_t hp = 0;
    uint16_t hitAttr = 0;
    float pos[3] = {};
    float quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    uint16_t motion0 = pose_wire::kNoMotion;
    uint16_t motion1 = pose_wire::kNoMotion;
    float frame0 = 0.0f;
    float frame1 = 0.0f;
    int32_t record[4] = {};
    uint32_t flags6a0c = 0;
};

inline Sample sampleOf(const Header& header, const PoseEntry& entry, uint32_t arrivalMs) {
    Sample sample;
    sample.senderMs = header.senderMs;
    sample.arrivalMs = arrivalMs;
    sample.seq = header.seq;
    sample.flags = entry.flags;
    sample.target = entry.target;
    sample.spawnRecord = entry.spawnRecord;
    sample.hp = entry.hp;
    sample.hitAttr = entry.hitAttr;
    std::copy(std::begin(entry.pos), std::end(entry.pos), std::begin(sample.pos));
    pose_wire::unpackQuat(entry.quat, sample.quat);
    sample.motion0 = entry.motion0;
    sample.motion1 = entry.motion1;
    sample.frame0 = entry.frame0;
    sample.frame1 = pose_wire::unpackFrame(entry.frame1);
    std::copy(std::begin(entry.record), std::end(entry.record), std::begin(sample.record));
    sample.flags6a0c = entry.flags6a0c;
    return sample;
}

// One decoded ENEMY_MOTION entry, numbered and timed on the simulator's clock; what `timed_queue` hands out.
struct MotionItem {
    uint32_t senderMs = 0;
    uint32_t seq = 0;
    uint8_t layer = 0;
    uint16_t motionNo = 0;
    uint32_t flags = 0;
    float interp = 0.0f;
    float start = 0.0f;
};

inline MotionItem itemOf(const Header& header, const MotionEntry& entry, uint32_t seq) {
    return {header.senderMs - entry.dtMs, seq, entry.layer, entry.motionNo, entry.flags, entry.interp, entry.start};
}

// ---- Timeline ----------------------------------------------------------------------------------------------------

enum class Reset : uint8_t { None, Gap, Teleport, Clock };

constexpr const char* name(Reset reason) {
    switch (reason) {
        case Reset::Gap: return "gap";
        case Reset::Teleport: return "teleport";
        case Reset::Clock: return "clock";
        case Reset::None: break;
    }
    return "none";
}

struct Shown {
    bool valid = false;         // a sample could be shown
    bool stale = false;         // nothing arrived for pose_timeline::kStaleMs: the enemy holds its last state
    Reset reset = Reset::None;  // a hard reset happened since the last render: the sample is placed, not blended
    Sample sample;
    size_t buffered = 0;
};

// One enemy's samples behind the shared peer clock, shown the clock's render delay late (the same instant as the
// peer's puppet): position and rotation interpolated, every discrete field from the older bracketing sample, nothing
// extrapolated. A gap, a jump or a restarted clock drops what was buffered and shows the newest sample at once.
class SlotTimeline {
public:
    void clear() {
        samples_.clear();
        placed_ = false;
        pending_ = Reset::None;
        hasArrival_ = false;
        hasGeneration_ = false;
    }

    void push(const stream_clock::StreamClock& clock, const Sample& sample) {
        followClock(clock);
        const bool known = std::any_of(samples_.begin(), samples_.end(), [&](const Sample& held) { return held.seq == sample.seq; });
        if (known) return;
        if (!samples_.empty() && stream_clock::after(sample.senderMs, samples_.back().senderMs)) {
            const Reset reason = hardResetReason(samples_.back(), sample);
            if (reason != Reset::None) {
                samples_.clear();
                placed_ = true;
                pending_ = reason;
            }
        }
        const auto at = std::find_if(samples_.begin(), samples_.end(), [&](const Sample& held) {
            return stream_clock::after(held.senderMs, sample.senderMs);
        });
        samples_.insert(at, sample);
        if (samples_.size() > kMaxSamples) samples_.erase(samples_.begin());
        lastArrivalMs_ = sample.arrivalMs;
        hasArrival_ = true;
    }

    Shown render(const stream_clock::StreamClock& clock, uint32_t nowMs) {
        Shown shown;
        followClock(clock);
        if (hasArrival_ && static_cast<int32_t>(nowMs - lastArrivalMs_) > static_cast<int32_t>(pose_timeline::kStaleMs)) {
            shown.stale = true;
            return shown;
        }
        if (!clock.ready() || samples_.empty()) return shown;
        const uint32_t target = clock.target(nowMs);
        size_t reached = 0;  // samples at or before the shown time
        while (reached < samples_.size() && !stream_clock::after(samples_[reached].senderMs, target)) ++reached;
        if (reached == 0) {
            if (!placed_) return shown;
            shown.sample = samples_.front();  // a reset: the newest sample is shown at once and held until its time comes
        } else {
            placed_ = false;
            samples_.erase(samples_.begin(), samples_.begin() + static_cast<std::ptrdiff_t>(reached - 1));  // keep the last one as the anchor
            shown.sample = samples_.size() > 1 ? interpolate(samples_[0], samples_[1], target) : samples_[0];
        }
        shown.valid = true;
        shown.buffered = samples_.size();
        shown.reset = pending_;
        pending_ = Reset::None;
        return shown;
    }

    // The newest sample received, whatever has been shown: what a hand-over adopts. Nothing before the first push.
    std::optional<Sample> newest() const {
        if (samples_.empty()) return std::nullopt;
        return samples_.back();
    }

    // The state at simulator time `target` between two samples.
    static Sample interpolate(const Sample& a, const Sample& b, uint32_t target) {
        const uint32_t span = b.senderMs - a.senderMs;
        const float t = span == 0 ? 0.0f : std::clamp(static_cast<float>(target - a.senderMs) / static_cast<float>(span), 0.0f, 1.0f);
        Sample out = a;
        position_blend::lerp(a.pos, b.pos, t, out.pos);
        position_blend::slerp(a.quat, b.quat, t, out.quat);
        if (a.motion0 == b.motion0 && b.frame0 >= a.frame0) out.frame0 = a.frame0 + (b.frame0 - a.frame0) * t;
        if (a.motion1 == b.motion1 && b.frame1 >= a.frame1) out.frame1 = a.frame1 + (b.frame1 - a.frame1) * t;
        return out;
    }

private:
    // The shared clock restarted: what was buffered is on the old mapping.
    void followClock(const stream_clock::StreamClock& clock) {
        if (hasGeneration_ && generation_ == clock.generation()) return;
        if (hasGeneration_) {
            samples_.clear();
            placed_ = true;
            pending_ = Reset::Clock;
            hasArrival_ = false;
        }
        generation_ = clock.generation();
        hasGeneration_ = true;
    }

    // A dead enemy is sampled rarely (enemy_observer_send_rule::kCorpseEveryRounds): its long intervals are no gap.
    Reset hardResetReason(const Sample& previous, const Sample& next) const {
        const bool sparse = lifeOf(previous.flags) != Life::Alive;
        const bool gap = next.arrivalMs - lastArrivalMs_ > pose_timeline::kGapResetMs ||
                         next.senderMs - previous.senderMs > pose_timeline::kGapResetMs;
        if (gap && !sparse) return Reset::Gap;
        return position_blend::distance(previous.pos, next.pos) >= pose_timeline::kTeleportUnits ? Reset::Teleport : Reset::None;
    }

    std::vector<Sample> samples_;  // by simulator time
    bool placed_ = false;
    Reset pending_ = Reset::None;
    uint32_t lastArrivalMs_ = 0;
    bool hasArrival_ = false;
    uint32_t generation_ = 0;
    bool hasGeneration_ = false;
};

}  // namespace enemy_observer_rule
