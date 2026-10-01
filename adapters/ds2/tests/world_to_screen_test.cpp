// world_to_screen::project against hand-computed cases (no game).
#include <cmath>
#include <cstdio>

#include "../src/world_to_screen.h"

namespace {

using world_to_screen::Camera;
using world_to_screen::project;
using world_to_screen::Vec3;

constexpr float kWidth = 1920, kHeight = 1080;
constexpr double kNinetyDegrees = 1.5707963267948966;
constexpr float kTolerance = 0.01f;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

bool near(float a, float b) { return std::fabs(a - b) < kTolerance; }

// Z-up camera at the origin looking along +Y with a 90 degree horizontal field of view (focal = width / 2).
Camera lookAlongY() {
    Camera c;
    c.right = {1, 0, 0};
    c.up = {0, 0, 1};
    c.forward = {0, 1, 0};
    c.horizontalFovRadians = kNinetyDegrees;
    return c;
}

}  // namespace

int main() {
    const Camera cam = lookAlongY();

    const auto center = project(cam, {0, 10, 0}, kWidth, kHeight);
    check(center && near(center->x, 960) && near(center->y, 540) && near(center->distance, 10), "straight ahead is the centre");

    const auto rightUp = project(cam, {5, 10, 5}, kWidth, kHeight);
    check(rightUp && near(rightUp->x, 960 + 480) && near(rightUp->y, 540 - 480), "right and up move right and up on screen");

    check(!project(cam, {0, -10, 0}, kWidth, kHeight), "behind the camera is not drawn");

    Camera moved = cam;
    moved.position = {100000.5, 200000.25, 50};
    const auto far = project(moved, {100000.5, 200010.25, 50}, kWidth, kHeight);
    check(far && near(far->x, 960) && near(far->y, 540), "large world coordinates keep precision");

    std::printf(g_failures ? "FAILED\n" : "PASS\n");
    return g_failures ? 1 : 0;
}
