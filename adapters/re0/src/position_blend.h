#pragma once
#include <cmath>

// Pure math of the remote-position correction (no game access, unit tested).
namespace position_blend {

constexpr float kDeadZone = 3.0f;          // below this drift nothing is done
constexpr float kSnapDistance = 60.0f;     // above this drift the character is snapped
constexpr float kBlendPerTick = 0.5f;      // share of the way toward the target moved per tick
constexpr float kMaxExtrapolationSeconds = 0.1f;
constexpr float kNearlyParallel = 0.9995f;  // |dot| above this: slerp falls back to nlerp

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

// The point `t` of the way from `from` to `to`.
inline void lerp(const float (&from)[3], const float (&to)[3], float t, float (&out)[3]) {
    for (int i = 0; i < 3; ++i) out[i] = from[i] + (to[i] - from[i]) * t;
}

// Normalized lerp of unit quaternions along the shorter arc.
inline void nlerp(const float (&from)[4], const float (&to)[4], float t, float (&out)[4]) {
    float dot = 0.0f;
    for (int i = 0; i < 4; ++i) dot += from[i] * to[i];
    const float sign = dot < 0.0f ? -1.0f : 1.0f;
    float length = 0.0f;
    for (int i = 0; i < 4; ++i) {
        out[i] = from[i] + (sign * to[i] - from[i]) * t;
        length += out[i] * out[i];
    }
    length = std::sqrt(length);
    if (length <= 0.0f) return;
    for (int i = 0; i < 4; ++i) out[i] /= length;
}

// Spherical interpolation of unit quaternions along the shorter arc (nlerp when they are nearly the same).
inline void slerp(const float (&from)[4], const float (&to)[4], float t, float (&out)[4]) {
    float dot = 0.0f;
    for (int i = 0; i < 4; ++i) dot += from[i] * to[i];
    const float sign = dot < 0.0f ? -1.0f : 1.0f;
    const float cosine = std::fabs(dot);
    if (cosine > kNearlyParallel) {
        nlerp(from, to, t, out);
        return;
    }
    const float angle = std::acos(cosine);
    const float sine = std::sin(angle);
    const float fromWeight = std::sin((1.0f - t) * angle) / sine;
    const float toWeight = sign * std::sin(t * angle) / sine;
    for (int i = 0; i < 4; ++i) out[i] = from[i] * fromWeight + to[i] * toWeight;
}

inline void blendPosition(const float (&current)[3], const float (&target)[3], float (&out)[3]) {
    lerp(current, target, kBlendPerTick, out);
}

inline void blendRotation(const float (&current)[4], const float (&target)[4], float (&out)[4]) {
    nlerp(current, target, kBlendPerTick, out);
}

}  // namespace position_blend
