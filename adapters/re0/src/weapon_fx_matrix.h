#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

// A muzzle matrix (the engine's 4x4 of floats, rotation in the upper 3x3 and the position in elements 12..14) as a
// position and a unit quaternion (pure, unit tested). The rotation is read and written in one fixed layout, so the
// matrix comes back exactly whichever way the engine multiplies.
namespace weapon_fx_matrix {

constexpr size_t kFloats = 16;
constexpr size_t kPositionStart = 12;
constexpr size_t kRowStride = 4;
constexpr float kFlipTolerance = 1e-6f;

// Components of the quaternion: x, y, z, w.
constexpr size_t kQuatX = 0;
constexpr size_t kQuatY = 1;
constexpr size_t kQuatZ = 2;
constexpr size_t kQuatW = 3;

inline float at(const float (&m)[kFloats], size_t row, size_t column) { return m[row * kRowStride + column]; }

inline void split(const float (&m)[kFloats], float (&pos)[3], float (&quat)[4]) {
    for (size_t i = 0; i < 3; ++i) pos[i] = m[kPositionStart + i];
    const float trace = at(m, 0, 0) + at(m, 1, 1) + at(m, 2, 2);
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        quat[kQuatW] = 0.25f * s;
        quat[kQuatX] = (at(m, 2, 1) - at(m, 1, 2)) / s;
        quat[kQuatY] = (at(m, 0, 2) - at(m, 2, 0)) / s;
        quat[kQuatZ] = (at(m, 1, 0) - at(m, 0, 1)) / s;
    } else if (at(m, 0, 0) > at(m, 1, 1) && at(m, 0, 0) > at(m, 2, 2)) {
        const float s = std::sqrt(1.0f + at(m, 0, 0) - at(m, 1, 1) - at(m, 2, 2)) * 2.0f;
        quat[kQuatW] = (at(m, 2, 1) - at(m, 1, 2)) / s;
        quat[kQuatX] = 0.25f * s;
        quat[kQuatY] = (at(m, 0, 1) + at(m, 1, 0)) / s;
        quat[kQuatZ] = (at(m, 0, 2) + at(m, 2, 0)) / s;
    } else if (at(m, 1, 1) > at(m, 2, 2)) {
        const float s = std::sqrt(1.0f + at(m, 1, 1) - at(m, 0, 0) - at(m, 2, 2)) * 2.0f;
        quat[kQuatW] = (at(m, 0, 2) - at(m, 2, 0)) / s;
        quat[kQuatX] = (at(m, 0, 1) + at(m, 1, 0)) / s;
        quat[kQuatY] = 0.25f * s;
        quat[kQuatZ] = (at(m, 1, 2) + at(m, 2, 1)) / s;
    } else {
        const float s = std::sqrt(1.0f + at(m, 2, 2) - at(m, 0, 0) - at(m, 1, 1)) * 2.0f;
        quat[kQuatW] = (at(m, 1, 0) - at(m, 0, 1)) / s;
        quat[kQuatX] = (at(m, 0, 2) + at(m, 2, 0)) / s;
        quat[kQuatY] = (at(m, 1, 2) + at(m, 2, 1)) / s;
        quat[kQuatZ] = 0.25f * s;
    }
    const float length = std::sqrt(quat[kQuatX] * quat[kQuatX] + quat[kQuatY] * quat[kQuatY] + quat[kQuatZ] * quat[kQuatZ] +
                                   quat[kQuatW] * quat[kQuatW]);
    if (length > kFlipTolerance) {
        for (float& component : quat) component /= length;
    }
}

// The inverse of `split`: identity in the fourth column and row, then the rotation and the position.
inline void join(const float (&pos)[3], const float (&quat)[4], float (&m)[kFloats]) {
    const float x = quat[kQuatX];
    const float y = quat[kQuatY];
    const float z = quat[kQuatZ];
    const float w = quat[kQuatW];
    std::fill(std::begin(m), std::end(m), 0.0f);
    m[0] = 1.0f - 2.0f * (y * y + z * z);
    m[1] = 2.0f * (x * y - z * w);
    m[2] = 2.0f * (x * z + y * w);
    m[4] = 2.0f * (x * y + z * w);
    m[5] = 1.0f - 2.0f * (x * x + z * z);
    m[6] = 2.0f * (y * z - x * w);
    m[8] = 2.0f * (x * z - y * w);
    m[9] = 2.0f * (y * z + x * w);
    m[10] = 1.0f - 2.0f * (x * x + y * y);
    for (size_t i = 0; i < 3; ++i) m[kPositionStart + i] = pos[i];
    m[15] = 1.0f;
}

}  // namespace weapon_fx_matrix
