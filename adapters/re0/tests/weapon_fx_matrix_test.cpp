// Checks the muzzle matrix round trip (no game): a rotation matrix split into position and quaternion and joined again
// comes back exactly, whichever of the four quaternion branches it takes. Exit 0 when every check passes.
#include <cmath>
#include <cstdio>

#include "../src/weapon_fx_matrix.h"

namespace {

namespace fm = weapon_fx_matrix;

constexpr float kTolerance = 1e-5f;
constexpr float kPi = 3.14159265f;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

// A unit quaternion for a turn of `angle` about the unit axis (x, y, z).
void turn(float angle, float x, float y, float z, float (&quat)[4]) {
    const float s = std::sin(angle * 0.5f);
    quat[fm::kQuatX] = x * s;
    quat[fm::kQuatY] = y * s;
    quat[fm::kQuatZ] = z * s;
    quat[fm::kQuatW] = std::cos(angle * 0.5f);
}

// The matrix of `quat` joined, split and joined again is the same matrix, and the position is untouched.
void roundTrip(float angle, float x, float y, float z, const char* what) {
    float quat[4];
    turn(angle, x, y, z, quat);
    const float pos[3] = {1.5f, -2.25f, 300.0f};
    float original[fm::kFloats];
    fm::join(pos, quat, original);
    float splitPos[3];
    float splitQuat[4];
    fm::split(original, splitPos, splitQuat);
    float again[fm::kFloats];
    fm::join(splitPos, splitQuat, again);
    bool same = true;
    for (size_t i = 0; i < fm::kFloats; ++i) same = same && std::fabs(original[i] - again[i]) < kTolerance;
    check(same, what);
    check(splitPos[0] == pos[0] && splitPos[1] == pos[1] && splitPos[2] == pos[2], "the position is untouched");
    const float length = std::sqrt(splitQuat[0] * splitQuat[0] + splitQuat[1] * splitQuat[1] + splitQuat[2] * splitQuat[2] +
                                   splitQuat[3] * splitQuat[3]);
    check(std::fabs(length - 1.0f) < kTolerance, "the split quaternion is a unit quaternion");
}

void testBranches() {
    roundTrip(0.0f, 0, 0, 1, "the identity (trace branch) round trips");
    roundTrip(0.7f, 0, 1, 0, "a small turn about y round trips");
    roundTrip(kPi, 1, 0, 0, "a half turn about x (x branch) round trips");
    roundTrip(kPi, 0, 1, 0, "a half turn about y (y branch) round trips");
    roundTrip(kPi, 0, 0, 1, "a half turn about z (z branch) round trips");
    roundTrip(2.5f, 0.57735f, 0.57735f, 0.57735f, "a turn about a diagonal axis round trips");
    roundTrip(-3.0f, 0.0f, 0.6f, 0.8f, "a negative turn round trips");
}

void testJoinShape() {
    const float pos[3] = {4.0f, 5.0f, 6.0f};
    const float identity[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float m[fm::kFloats];
    fm::join(pos, identity, m);
    check(m[0] == 1.0f && m[5] == 1.0f && m[10] == 1.0f && m[15] == 1.0f, "the identity rotation has a unit diagonal");
    check(m[3] == 0.0f && m[7] == 0.0f && m[11] == 0.0f, "the fourth column stays zero");
    check(m[12] == 4.0f && m[13] == 5.0f && m[14] == 6.0f, "the position sits in elements 12 to 14");
}

}  // namespace

int main() {
    testBranches();
    testJoinShape();
    if (g_failures == 0) std::printf("weapon_fx_matrix_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
