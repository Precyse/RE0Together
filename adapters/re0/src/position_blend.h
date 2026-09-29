#pragma once
#include <cmath>

// Pure math of the remote-position correction (no game access, unit tested).
namespace position_blend {

constexpr float kDeadZone = 3.0f;          // below this drift nothing is done
constexpr float kSnapDistance = 60.0f;     // above this drift the character is snapped
constexpr float kBlendPerTick = 0.5f;      // share of the way toward the target moved per tick
constexpr float kMaxExtrapolationSeconds = 0.1f;

enum class Action { Ignore, Blend, Snap };

inline Action classify(float drift) {
    if (drift < kDeadZone) return Action::Ignore;
    return drift <= kSnapDistance ? Action::Blend : Action::Snap;
}

inline float distance(const float (&a)[3], const float (&b)[3]) {
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

// Units per second from two positions `seconds` apart; zero when the interval is not positive.
inline void velocity(const float (&from)[3], const float (&to)[3], float seconds, float (&out)[3]) {
    for (int i = 0; i < 3; ++i) out[i] = seconds > 0.0f ? (to[i] - from[i]) / seconds : 0.0f;
}

// `pos` advanced by `velocity` for `elapsedSeconds`, capped at kMaxExtrapolationSeconds.
inline void extrapolate(const float (&pos)[3], const float (&velocity)[3], float elapsedSeconds, float (&out)[3]) {
    const float seconds = elapsedSeconds < 0.0f ? 0.0f : std::fmin(elapsedSeconds, kMaxExtrapolationSeconds);
    for (int i = 0; i < 3; ++i) out[i] = pos[i] + velocity[i] * seconds;
}

inline void blendPosition(const float (&current)[3], const float (&target)[3], float (&out)[3]) {
    for (int i = 0; i < 3; ++i) out[i] = current[i] + (target[i] - current[i]) * kBlendPerTick;
}

// Normalized lerp of unit quaternions along the shorter arc.
inline void blendRotation(const float (&current)[4], const float (&target)[4], float (&out)[4]) {
    float dot = 0.0f;
    for (int i = 0; i < 4; ++i) dot += current[i] * target[i];
    const float sign = dot < 0.0f ? -1.0f : 1.0f;
    float length = 0.0f;
    for (int i = 0; i < 4; ++i) {
        out[i] = current[i] + (sign * target[i] - current[i]) * kBlendPerTick;
        length += out[i] * out[i];
    }
    length = std::sqrt(length);
    if (length <= 0.0f) return;
    for (int i = 0; i < 4; ++i) out[i] /= length;
}

}  // namespace position_blend
