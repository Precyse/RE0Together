#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "pose_wire.h"

// Pure rules of the pose puppet (unit tested): which flags a replayed motion request keeps and when the puppet's own
// layer-0 motion has drifted far enough from the owner's to be requested again at the owner's frame.
namespace puppet_rule {

constexpr uint32_t kKeepRootMotionFlag = 0x10;  // playMotion's "keep the motion's own displacement": the position is the network's
constexpr float kSeekFrames = 6.0f;             // layer-0 frames apart on the same motion before the puppet seeks
constexpr uint32_t kSeekHoldTicks = 6;          // consecutive moves a difference must last (a request lands a frame later)
constexpr float kLoopHalf = 0.5f;

// The flags a request is replayed with: a layer-0 request never keeps the motion's own displacement.
inline uint32_t replayFlags(uint32_t layer, uint32_t flags) { return layer == 0 ? flags & ~kKeepRootMotionFlag : flags; }

// Distance between two frames of one motion; a looping motion wraps at its last frame (frameMax, 0 = unknown).
inline float frameDistance(float a, float b, float frameMax) {
    const float direct = std::fabs(a - b);
    return frameMax > 0.0f && direct > frameMax * kLoopHalf ? std::max(0.0f, frameMax - direct) : direct;
}

// The puppet's layer 0 as the engine holds it.
struct Layer0 {
    uint16_t motion;
    float frame;
    float frameMax;
};

enum class Verdict { Follow, Seek };

// Compares the puppet's layer 0 with the owner's, once per puppet move. A timeline reset asks for a seek at once.
class SeekTracker {
public:
    Verdict update(const Layer0& puppet, const pose_wire::Layer& owner) {
        if (owner.motion == pose_wire::kNoMotion) {
            ticks_ = 0;
            return Verdict::Follow;
        }
        if (seekPending_) {
            seekPending_ = false;
            ticks_ = 0;
            return Verdict::Seek;
        }
        const bool apart = puppet.motion != owner.motion || frameDistance(puppet.frame, owner.frame, puppet.frameMax) > kSeekFrames;
        if (!apart) {
            ticks_ = 0;
            return Verdict::Follow;
        }
        if (++ticks_ < kSeekHoldTicks) return Verdict::Follow;
        ticks_ = 0;
        return Verdict::Seek;
    }

    // The next update seeks whatever the difference (the owner was placed, not blended).
    void requestSeek() { seekPending_ = true; }

    void reset() {
        ticks_ = 0;
        seekPending_ = false;
    }

private:
    uint32_t ticks_ = 0;
    bool seekPending_ = false;
};

}  // namespace puppet_rule
