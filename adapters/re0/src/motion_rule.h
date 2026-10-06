#pragma once
#include <cstdint>

// Pure rules for keeping a model's animation (motion number and frame) close to its owner's. No game access, unit tested.
namespace motion_rule {

constexpr float kFramesPerSecond = 30.0f;  // motion frames the owner's animation advances per second
constexpr float kFrameTolerance = 4.0f;    // a frame this close to the owner's is left alone
constexpr float kMaxExtrapolationSeconds = 0.1f;
constexpr int64_t kMismatchHoldMs = 400;   // a character's motion must differ this long before it is forced

// The owner's frame `elapsedSeconds` after it was reported (capped like positions).
inline float advancedFrame(float frame, float elapsedSeconds) {
    if (elapsedSeconds < 0.0f) elapsedSeconds = 0.0f;
    if (elapsedSeconds > kMaxExtrapolationSeconds) elapsedSeconds = kMaxExtrapolationSeconds;
    return frame + elapsedSeconds * kFramesPerSecond;
}

enum class Step { Keep, SetFrame, SetMotion };

// A different motion number is switched; the same one is only re-timed when its frame drifted.
inline Step stepFor(uint16_t localMotion, float localFrame, uint16_t targetMotion, float targetFrame) {
    if (localMotion != targetMotion) return Step::SetMotion;
    const float drift = localFrame > targetFrame ? localFrame - targetFrame : targetFrame - localFrame;
    return drift > kFrameTolerance ? Step::SetFrame : Step::Keep;
}

// For characters whose own replayed input animates them: only a mismatch that persists is a stuck animation, a
// short one is just the input arriving a little earlier or later.
struct MismatchClock {
    int64_t sinceMs = -1;

    bool due(bool mismatch, int64_t nowMs) {
        if (!mismatch) {
            sinceMs = -1;
            return false;
        }
        if (sinceMs < 0) sinceMs = nowMs;
        return nowMs - sinceMs >= kMismatchHoldMs;
    }
};

}  // namespace motion_rule
