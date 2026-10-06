// world_to_screen::project against hand-computed cases (no game).
#include <cmath>
#include <cstdio>
#include <numbers>

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

    constexpr float kMargin = 40;
    check(!world_to_screen::edgeArrow(cam, {0, 10, 0}, kWidth, kHeight, kMargin), "an on-screen point has no arrow");
    const auto right = world_to_screen::edgeArrow(cam, {50, 10, 0}, kWidth, kHeight, kMargin);
    check(right && near(right->x, kWidth - kMargin) && near(right->y, 540) && near(right->angle, 0),
          "a point far to the right sits on the right edge pointing right");
    const auto behindLeft = world_to_screen::edgeArrow(cam, {-5, -10, 0}, kWidth, kHeight, kMargin);
    check(behindLeft && near(behindLeft->x, kMargin) && near(std::cos(behindLeft->angle), -1),
          "behind and to the left points left on the left edge");
    const auto straightBehind = world_to_screen::edgeArrow(cam, {0, -10, 0}, kWidth, kHeight, kMargin);
    check(straightBehind && near(straightBehind->x, 960) && near(straightBehind->y, kHeight - kMargin) &&
              near(straightBehind->angle, static_cast<float>(std::numbers::pi / 2)),
          "exactly behind points down at the bottom edge");
    const auto above = world_to_screen::edgeArrow(cam, {0, 10, 40}, kWidth, kHeight, kMargin);
    check(above && near(above->y, kMargin) && near(above->angle, static_cast<float>(-std::numbers::pi / 2)),
          "a point far above sits on the top edge pointing up");

    std::printf(g_failures ? "FAILED\n" : "PASS\n");
    return g_failures ? 1 : 0;
}
