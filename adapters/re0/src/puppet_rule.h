#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

#include "pose_wire.h"

// Pure rules of the pose puppet (unit tested): which flags a replayed motion request keeps, when the puppet's own
// layer-0 motion has drifted far enough from the owner's to be requested again at the owner's frame, which anim control
// a request belongs to, which requests the hand pose follows, the hit attribute diff and when the held weapon needs a
// re-equip.
namespace puppet_rule {

constexpr uint32_t kKeepRootMotionFlag = 0x10;  // playMotion's "keep the motion's own displacement": the position is the network's
constexpr float kSeekFrames = 6.0f;             // layer-0 frames apart on the same motion before the puppet seeks
constexpr uint32_t kSeekHoldTicks = 6;          // consecutive moves a difference must last (a request lands a frame later)
// Moves after a seek before frame drift alone may seek again (a second at 60 fps). Drift on the motion just sought means
// the two copies play it at another rate; restarting it every hold replays the clip in steps (2026-10-25 live: an
// observed zombie re-sought ~7 times a second through its death fall).
constexpr uint32_t kDriftSeekCooldownTicks = 60;
// The owner holds one frame of a motion (2026-10-25 live: a zombie lying in wait on 0x1b frame 0 on the simulator,
// standing up over and over on the observer, which played the clip on and was sought back every cooldown): once the
// owner's frame has not moved for kStillOwnerTicks moves, a puppet that left it by more than kStillSeekFrames seeks at
// once, so the copy holds the frame too.
constexpr uint32_t kStillOwnerTicks = 10;
constexpr float kStillFrameEpsilon = 0.01f;
constexpr float kStillSeekFrames = 1.0f;
constexpr float kLoopHalf = 0.5f;
constexpr uint32_t kWeaponRefreshMs = 1000;  // the held weapon differs from the owner's this long, with no re-equip pending

// The flags a request is replayed with: a layer-0 request never keeps the motion's own displacement.
inline uint32_t replayFlags(uint32_t layer, uint32_t flags) { return layer == 0 ? flags & ~kKeepRootMotionFlag : flags; }

// Where a character's anim controls sit, as offsets from the character: the body's, and sub-control k at
// firstSub + k * subStride (the weapon is k = 3).
struct ControlLayout {
    uintptr_t body;
    uintptr_t firstSub;
    uintptr_t subStride;
};

// The MotionEvent target of the anim control at `controlOffset` from its character: kBodyTarget, 1 + k for sub-control
// k, or nothing for a control the puppet does not mirror.
inline std::optional<uint16_t> controlTarget(const ControlLayout& layout, uintptr_t controlOffset) {
    if (controlOffset == layout.body) return pose_wire::kBodyTarget;
    if (controlOffset < layout.firstSub) return std::nullopt;
    const uintptr_t inSubs = controlOffset - layout.firstSub;
    if (inSubs % layout.subStride != 0 || inSubs / layout.subStride >= pose_wire::kSubControls) return std::nullopt;
    return static_cast<uint16_t>(inSubs / layout.subStride + 1);
}

// The offset from the character of the anim control a MotionEvent target names, or nothing for a target out of range.
inline std::optional<uintptr_t> controlOffset(const ControlLayout& layout, uint16_t target) {
    if (target == pose_wire::kBodyTarget) return layout.body;
    if (target > pose_wire::kSubControls) return std::nullopt;
    return layout.firstSub + (target - 1) * layout.subStride;
}

// The engine refreshes the hand pose (0x50f910) after a layer-0 body request; a replayed one does not, so it is called
// after each of those. Sub-controls and the other layers leave the hand alone.
inline bool needsHandPose(uint32_t layer, uint16_t target) { return layer == 0 && target == pose_wire::kBodyTarget; }

// The bits to set and to clear on a character's hit attributes to make them `wanted`.
struct HitChange {
    uint32_t set;
    uint32_t clear;
};

inline HitChange hitChange(uint32_t current, uint32_t wanted) {
    current &= pose_wire::kHitAttributeMask;
    wanted &= pose_wire::kHitAttributeMask;
    return {wanted & ~current, current & ~wanted};
}

// A weapon type that differs from the owner's for more than a second with no re-equip under way is one the inventory
// stream did not fix (a type change that skipped the menu): the puppet re-equips from the mirrored block.
inline bool needsWeaponRefresh(uint8_t poseWeapon, int32_t puppetWeapon, uint32_t sinceMs, bool refreshPending) {
    return !refreshPending && puppetWeapon != static_cast<int32_t>(poseWeapon) && sinceMs > kWeaponRefreshMs;
}

// Distance between two frames of one motion; a looping motion wraps at its last frame (frameMax, 0 = unknown).
inline float frameDistance(float a, float b, float frameMax) {
    const float direct = std::fabs(a - b);
    return frameMax > 0.0f && direct > frameMax * kLoopHalf ? std::max(0.0f, frameMax - direct) : direct;
}

// One motion layer of a character as the engine holds it.
struct LayerFrame {
    uint16_t motion;
    float frame;
    float frameMax;
};

enum class Verdict { Follow, Seek, Hold };  // Hold: seek to the owner's still frame (every few moves, so not logged)

// Compares one of the puppet's layers with the owner's, once per puppet move. A timeline reset asks for a seek at once. A
// different motion seeks after the hold; frame drift on the same motion only when `driftAllowed` and no seek ran in the
// last kDriftSeekCooldownTicks moves. With `holdsStillOwner`, an owner frame that stays put is held (kStillOwnerTicks).
class SeekTracker {
public:
    Verdict update(const LayerFrame& puppet, const pose_wire::Layer& owner, bool driftAllowed = true, bool holdsStillOwner = false) {
        if (sinceSeek_ < kDriftSeekCooldownTicks) ++sinceSeek_;
        const bool ownerStill = owner.motion == lastOwnerMotion_ && std::fabs(owner.frame - lastOwnerFrame_) < kStillFrameEpsilon;
        stillTicks_ = ownerStill ? stillTicks_ + 1 : 0;
        lastOwnerMotion_ = owner.motion;
        lastOwnerFrame_ = owner.frame;
        if (owner.motion == pose_wire::kNoMotion) {
            ticks_ = 0;
            return Verdict::Follow;
        }
        if (seekPending_) return seek();
        const bool otherMotion = puppet.motion != owner.motion;
        if (holdsStillOwner && !otherMotion && stillTicks_ >= kStillOwnerTicks &&
            frameDistance(puppet.frame, owner.frame, puppet.frameMax) > kStillSeekFrames) {
            seek();
            return Verdict::Hold;
        }
        const bool drift = !otherMotion && frameDistance(puppet.frame, owner.frame, puppet.frameMax) > kSeekFrames;
        const bool apart = otherMotion || (drift && driftAllowed && sinceSeek_ >= kDriftSeekCooldownTicks);
        if (!apart) {
            ticks_ = 0;
            return Verdict::Follow;
        }
        if (++ticks_ < kSeekHoldTicks) return Verdict::Follow;
        return seek();
    }

    // The next update seeks whatever the difference (the owner was placed, not blended).
    void requestSeek() { seekPending_ = true; }

    void reset() {
        ticks_ = 0;
        seekPending_ = false;
        sinceSeek_ = kDriftSeekCooldownTicks;
    }

private:
    Verdict seek() {
        seekPending_ = false;
        ticks_ = 0;
        sinceSeek_ = 0;
        return Verdict::Seek;
    }

    uint32_t ticks_ = 0;
    bool seekPending_ = false;
    uint32_t sinceSeek_ = kDriftSeekCooldownTicks;
    uint32_t stillTicks_ = 0;  // moves the owner's frame has stayed put
    uint16_t lastOwnerMotion_ = pose_wire::kNoMotion;
    float lastOwnerFrame_ = 0.0f;
};

// Times how long the puppet's weapon has differed from the owner's, once per puppet move.
class WeaponWatch {
public:
    // True when a re-equip is due; the timing then starts over, so a mismatch no re-equip fixes asks once a second.
    bool update(uint8_t poseWeapon, int32_t puppetWeapon, uint32_t nowMs, bool refreshPending) {
        if (puppetWeapon == static_cast<int32_t>(poseWeapon) || refreshPending) {
            differing_ = false;
            return false;
        }
        if (!differing_) {
            differing_ = true;
            sinceMs_ = nowMs;
            return false;
        }
        if (!needsWeaponRefresh(poseWeapon, puppetWeapon, nowMs - sinceMs_, refreshPending)) return false;
        sinceMs_ = nowMs;
        return true;
    }

    void reset() { differing_ = false; }

private:
    bool differing_ = false;
    uint32_t sinceMs_ = 0;
};

}  // namespace puppet_rule
