#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

#include "pose_wire.h"
#include "position_blend.h"

// The receiver's delayed timeline of the owner's poses and motion events (pure, unit tested). Both streams carry the
// owner's clock; the timeline maps it to the local clock by the smallest transit time seen, shows the owner's state
// `kRenderDelayMs` behind that, and hands out every motion event whose time has come, once, in order.
namespace pose_timeline {

constexpr uint32_t kRenderDelayMs = 100;
constexpr size_t kMaxPoses = 64;
constexpr size_t kMaxEvents = 64;
constexpr int32_t kStaleEventMs = 1000;         // an event this far behind the render time is not replayed
constexpr int32_t kClockResetMs = 5000;         // a transit time this far off the tracked one: the sender's clock restarted
constexpr uint32_t kOffsetRelaxSamples = 32;    // a later-than-tracked sample moves the tracked transit time 1 ms per this many
constexpr float kNearestHalf = 0.5f;

// Wrap-safe "a is later than b" for the owner's millisecond clock and the sequence numbers.
inline bool after(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) > 0; }

struct Result {
    bool valid = false;         // a pose could be shown
    uint32_t staleDropped = 0;  // events that were due but too old to replay
};

class Timeline {
public:
    void clear() {
        poses_.clear();
        events_.clear();
        hasOffset_ = false;
        hasHandedOut_ = false;
        laterSamples_ = 0;
    }

    void addPose(const pose_wire::Pose& pose, uint32_t arrivalMs) {
        observe(pose.senderMs, arrivalMs);
        for (const pose_wire::Pose& known : poses_) {
            if (known.seq == pose.seq) return;
        }
        const auto at = std::find_if(poses_.begin(), poses_.end(), [&](const pose_wire::Pose& known) {
            return after(known.senderMs, pose.senderMs);
        });
        poses_.insert(at, pose);
        if (poses_.size() > kMaxPoses) poses_.erase(poses_.begin());
    }

    void addEvent(const pose_wire::MotionEvent& event, uint32_t arrivalMs) {
        observe(event.senderMs, arrivalMs);
        if (hasHandedOut_ && !after(event.seq, lastHandedOut_)) return;
        for (const pose_wire::MotionEvent& known : events_) {
            if (known.seq == event.seq) return;
        }
        const auto at = std::find_if(events_.begin(), events_.end(), [&](const pose_wire::MotionEvent& known) {
            return after(known.seq, event.seq);
        });
        events_.insert(at, event);
        if (events_.size() > kMaxEvents) events_.erase(events_.begin());
    }

    // The owner's state at `nowMs` minus the render delay, into `out`; the events due by then are appended to `due`.
    Result render(uint32_t nowMs, pose_wire::Pose& out, std::vector<pose_wire::MotionEvent>& due) {
        Result result;
        if (!hasOffset_) return result;
        const uint32_t target = nowMs - static_cast<uint32_t>(offset_) - kRenderDelayMs;
        size_t reached = 0;  // poses at or before the render time
        while (reached < poses_.size() && !after(poses_[reached].senderMs, target)) ++reached;
        if (reached == 0) return result;
        poses_.erase(poses_.begin(), poses_.begin() + static_cast<std::ptrdiff_t>(reached - 1));  // keep the last one as the anchor
        out = poses_.size() > 1 ? interpolate(poses_[0], poses_[1], target) : poses_[0];
        result.valid = true;
        result.staleDropped = takeDue(target, due);
        return result;
    }

    // The state at owner time `target` between two samples; the discrete fields come from the nearer one.
    static pose_wire::Pose interpolate(const pose_wire::Pose& a, const pose_wire::Pose& b, uint32_t target) {
        const uint32_t span = b.senderMs - a.senderMs;
        const float t = span == 0 ? 0.0f : std::clamp(static_cast<float>(target - a.senderMs) / static_cast<float>(span), 0.0f, 1.0f);
        pose_wire::Pose out = t < kNearestHalf ? a : b;
        position_blend::lerp(a.pos, b.pos, t, out.pos);
        position_blend::slerp(a.quat, b.quat, t, out.quat);
        for (size_t i = 0; i < 2; ++i) {
            const bool sameMotion = a.layer[i].motion == b.layer[i].motion && b.layer[i].frame >= a.layer[i].frame;
            if (sameMotion) out.layer[i].frame = a.layer[i].frame + (b.layer[i].frame - a.layer[i].frame) * t;
        }
        return out;
    }

private:
    // Tracks the smallest transit time (arrival minus sender clock); the clock restarting on the other side resets all.
    void observe(uint32_t senderMs, uint32_t arrivalMs) {
        const int32_t transit = static_cast<int32_t>(arrivalMs - senderMs);
        if (!hasOffset_) {
            offset_ = transit;
            hasOffset_ = true;
            return;
        }
        const int32_t gap = transit - offset_;
        if (gap > kClockResetMs || gap < -kClockResetMs) {
            clear();
            offset_ = transit;
            hasOffset_ = true;
        } else if (gap < 0) {
            offset_ = transit;
        } else if (gap > 0 && ++laterSamples_ >= kOffsetRelaxSamples) {
            ++offset_;
            laterSamples_ = 0;
        }
    }

    uint32_t takeDue(uint32_t target, std::vector<pose_wire::MotionEvent>& due) {
        uint32_t stale = 0;
        size_t taken = 0;
        while (taken < events_.size() && !after(events_[taken].senderMs, target)) {
            const pose_wire::MotionEvent& event = events_[taken++];
            hasHandedOut_ = true;
            lastHandedOut_ = event.seq;
            if (static_cast<int32_t>(target - event.senderMs) > kStaleEventMs) {
                ++stale;
            } else {
                due.push_back(event);
            }
        }
        events_.erase(events_.begin(), events_.begin() + static_cast<std::ptrdiff_t>(taken));
        return stale;
    }

    std::vector<pose_wire::Pose> poses_;           // by owner time
    std::vector<pose_wire::MotionEvent> events_;   // by sequence
    bool hasOffset_ = false;
    int32_t offset_ = 0;
    uint32_t laterSamples_ = 0;
    bool hasHandedOut_ = false;
    uint32_t lastHandedOut_ = 0;
};

}  // namespace pose_timeline
