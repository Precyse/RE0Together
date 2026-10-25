#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

#include "pose_wire.h"
#include "position_blend.h"
#include "stream_clock.h"
#include "timed_queue.h"

// The receiver's delayed timeline of the owner's poses and motion events (pure, unit tested). The owner's clock is
// mapped to the local one by the peer's shared StreamClock; the timeline shows the owner's state the clock's render delay
// behind and hands out every motion event whose time has come, once, in order. A hard reset (room or load serial
// change, a gap, a teleport, a clock re-anchor) drops what was buffered and shows the newest pose at once; nothing is
// extrapolated: past the newest pose the last one is held.
namespace pose_timeline {

constexpr size_t kMaxPoses = 64;
constexpr uint32_t kGapResetMs = 500;          // a sender or arrival gap this long between poses is a hard reset
constexpr int32_t kMaxLeadPastDelayMs = 150;   // the newest pose this far past the render delay ahead of the shown time: re-anchor
constexpr float kTeleportUnits = 2000.0f;      // a jump this long between two poses is a hard reset (probe E1)
constexpr size_t kMaxPosesPer100MsDelay = 8;   // more buffered poses ahead of the shown time than this per 100 ms of delay: re-anchor
constexpr uint32_t kDepthDelayUnitMs = 100;
constexpr uint32_t kStaleMs = 2000;            // no pose, held or not, for this long: the owner is lost, the puppet holds

enum class Reset : uint8_t { None, Room, Serial, Gap, Lead, Teleport, Depth, Clock };

inline const char* name(Reset reason) {
    switch (reason) {
        case Reset::Room: return "room";
        case Reset::Serial: return "serial";
        case Reset::Gap: return "gap";
        case Reset::Lead: return "lead";
        case Reset::Teleport: return "teleport";
        case Reset::Depth: return "depth";
        case Reset::Clock: return "clock";
        case Reset::None: break;
    }
    return "none";
}

struct Result {
    bool valid = false;         // a pose could be shown
    bool stale = false;         // the owner has sent nothing for `kStaleMs`: nothing is shown, the puppet holds
    Reset reset = Reset::None;  // a hard reset happened since the last render: the pose is placed, not blended
    uint32_t staleDropped = 0;  // events that were due but too old to replay
    size_t buffered = 0;        // poses held
    int32_t behindMs = 0;       // how far the shown time is behind the newest pose
};

class Timeline {
public:
    // Drops everything, including the stale clock and sequence numbering (the peer left or changed).
    void clear() {
        poses_.clear();
        events_.clear();
        placed_ = false;
        pending_ = Reset::None;
        hasArrival_ = false;
        hasGeneration_ = false;
    }

    void addPose(stream_clock::StreamClock& clock, const pose_wire::Pose& pose, uint32_t arrivalMs) {
        clock.observe(pose.senderMs, arrivalMs);
        followClock(clock);
        if (std::any_of(poses_.begin(), poses_.end(), [&](const pose_wire::Pose& known) { return known.seq == pose.seq; })) return;
        const int32_t transit = static_cast<int32_t>(arrivalMs - pose.senderMs);
        if (!poses_.empty() && stream_clock::after(pose.senderMs, poses_.back().senderMs)) {
            const Reset reason = hardResetReason(poses_.back(), pose, arrivalMs);
            if (reason != Reset::None) {
                poses_.clear();
                placed_ = true;
                pending_ = reason;
                if (reason == Reset::Gap) {
                    clock.reanchor(transit);
                    generation_ = clock.generation();
                }
            }
        }
        const auto at = std::find_if(poses_.begin(), poses_.end(), [&](const pose_wire::Pose& known) {
            return stream_clock::after(known.senderMs, pose.senderMs);
        });
        if (at == poses_.end()) newestTransit_ = transit;
        poses_.insert(at, pose);
        if (poses_.size() > kMaxPoses) poses_.erase(poses_.begin());
        lastArrivalMs_ = arrivalMs;
        hasArrival_ = true;
    }

    void addEvent(stream_clock::StreamClock& clock, const pose_wire::MotionEvent& event, uint32_t arrivalMs) {
        clock.observe(event.senderMs, arrivalMs);
        followClock(clock);
        events_.push(event);
    }

    // The owner's state at local time `nowMs` minus the render delay, into `out`; the events due by then are appended
    // to `due`.
    Result render(stream_clock::StreamClock& clock, uint32_t nowMs, pose_wire::Pose& out, std::vector<pose_wire::MotionEvent>& due) {
        Result result;
        followClock(clock);
        if (hasArrival_ && static_cast<int32_t>(nowMs - lastArrivalMs_) > static_cast<int32_t>(kStaleMs)) {
            result.stale = true;
            result.buffered = poses_.size();
            return result;
        }
        if (!clock.ready() || poses_.empty()) return result;
        uint32_t target = clock.target(nowMs);
        if (const Reset reason = pending_ == Reset::None ? clockTrouble(target, clock.delayMs()) : Reset::None; reason != Reset::None) {
            clock.reanchor(newestTransit_);
            generation_ = clock.generation();
            poses_.erase(poses_.begin(), poses_.end() - 1);
            placed_ = true;
            pending_ = reason;
            target = clock.target(nowMs);
        }
        size_t reached = 0;  // poses at or before the render time
        while (reached < poses_.size() && !stream_clock::after(poses_[reached].senderMs, target)) ++reached;
        if (reached == 0) {
            if (!placed_) return result;
            out = poses_.front();  // a reset: the newest pose is shown at once and held until its time comes
        } else {
            placed_ = false;
            poses_.erase(poses_.begin(), poses_.begin() + static_cast<std::ptrdiff_t>(reached - 1));  // keep the last one as the anchor
            out = poses_.size() > 1 ? interpolate(poses_[0], poses_[1], target) : poses_[0];
        }
        result.valid = true;
        result.buffered = poses_.size();
        result.reset = pending_;
        pending_ = Reset::None;
        result.behindMs = static_cast<int32_t>(poses_.back().senderMs - target);
        result.staleDropped = events_.takeDue(target, due);
        return result;
    }

    // The state at owner time `target` between two samples: position, rotation, frames and pitch are interpolated, every
    // discrete field is the older sample's.
    static pose_wire::Pose interpolate(const pose_wire::Pose& a, const pose_wire::Pose& b, uint32_t target) {
        const uint32_t span = b.senderMs - a.senderMs;
        const float t = span == 0 ? 0.0f : std::clamp(static_cast<float>(target - a.senderMs) / static_cast<float>(span), 0.0f, 1.0f);
        pose_wire::Pose out = a;
        position_blend::lerp(a.pos, b.pos, t, out.pos);
        position_blend::slerp(a.quat, b.quat, t, out.quat);
        for (size_t i = 0; i < 2; ++i) {
            const bool sameMotion = a.layer[i].motion == b.layer[i].motion && b.layer[i].frame >= a.layer[i].frame;
            if (sameMotion) out.layer[i].frame = a.layer[i].frame + (b.layer[i].frame - a.layer[i].frame) * t;
        }
        out.manualPitch = a.manualPitch + (b.manualPitch - a.manualPitch) * t;
        return out;
    }

private:
    // Another stream (or the clock itself) restarted the shared clock: what was buffered is on the old mapping.
    void followClock(const stream_clock::StreamClock& clock) {
        if (hasGeneration_ && generation_ == clock.generation()) return;
        if (hasGeneration_) {
            poses_.clear();
            events_.clear();
            placed_ = true;
            pending_ = Reset::Clock;
            hasArrival_ = false;
        }
        generation_ = clock.generation();
        hasGeneration_ = true;
    }

    Reset hardResetReason(const pose_wire::Pose& previous, const pose_wire::Pose& next, uint32_t arrivalMs) const {
        if (next.loadSerial != previous.loadSerial) return Reset::Serial;
        if (next.room != previous.room) return Reset::Room;
        if (arrivalMs - lastArrivalMs_ > kGapResetMs || next.senderMs - previous.senderMs > kGapResetMs) {
            return Reset::Gap;
        }
        if (position_blend::distance(previous.pos, next.pos) >= kTeleportUnits) return Reset::Teleport;
        return Reset::None;
    }

    // The mapped clock has drifted from the newest pose: it is ahead of the shown time by more than the render delay plus
    // `kMaxLeadPastDelayMs`, or more poses wait beyond it than the delay should hold. Both scale with the adaptive delay.
    Reset clockTrouble(uint32_t target, uint32_t delayMs) const {
        if (static_cast<int32_t>(poses_.back().senderMs - target) > static_cast<int32_t>(delayMs) + kMaxLeadPastDelayMs) return Reset::Lead;
        const size_t ahead = static_cast<size_t>(std::count_if(poses_.begin(), poses_.end(), [&](const pose_wire::Pose& pose) {
            return stream_clock::after(pose.senderMs, target);
        }));
        const size_t allowed = kMaxPosesPer100MsDelay * std::max(delayMs, kDepthDelayUnitMs) / kDepthDelayUnitMs;
        return ahead > allowed ? Reset::Depth : Reset::None;
    }

    std::vector<pose_wire::Pose> poses_;  // by owner time
    timed_queue::TimedQueue<pose_wire::MotionEvent> events_;
    bool placed_ = false;                 // a reset happened: the oldest pose is shown before its time
    Reset pending_ = Reset::None;         // reported by the next render
    int32_t newestTransit_ = 0;           // arrival minus owner clock of the newest pose
    uint32_t lastArrivalMs_ = 0;
    bool hasArrival_ = false;
    uint32_t generation_ = 0;
    bool hasGeneration_ = false;
};

}  // namespace pose_timeline
