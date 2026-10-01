// load_shape: stack placement, box corners and the screen hull (no game).
#include <cmath>
#include <cstdio>

#include "../src/load_shape.h"

namespace {

constexpr double kTolerance = 1e-6;
constexpr float kHalfTurn = 3.14159265f;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

bool near(double a, double b) { return std::fabs(a - b) < kTolerance; }

}  // namespace

int main() {
    using namespace load_shape;
    const world_to_screen::Vec3 feet{100, 200, 50};

    const auto facingY = stack(feet, 0.0f, 3);
    check(facingY.size() == 3, "one box per piece");
    check(near(facingY[0].centre.y, 200 - kStackBehind), "facing +Y: load sits behind");
    const bool sideBySide = near(facingY[1].centre.x - facingY[0].centre.x, kPieceWidth);
    check(near(facingY[0].centre.x + facingY[1].centre.x, 200) && sideBySide,
          "the first row is two boxes side by side, centred on the back");
    check(near(facingY[0].centre.z, 50 + kStackBase + kPieceHeight / 2),
          "the first row starts at the small of the back");
    check(near(facingY[2].centre.z - facingY[0].centre.z, kPieceHeight), "the next row sits one height up");

    const auto facingMinusY = stack(feet, kHalfTurn, 1);
    check(std::fabs(facingMinusY[0].centre.y - (200 + kStackBehind)) < 1e-4, "facing -Y: load sits on the +Y side");
    check(stack(feet, 0.0f, 40).size() == static_cast<size_t>(kMaxShown), "a tall load is capped");

    const auto box = corners(facingY[0]);
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (const auto& c : box) {
        minX = std::fmin(minX, c.x), maxX = std::fmax(maxX, c.x);
        minY = std::fmin(minY, c.y), maxY = std::fmax(maxY, c.y);
    }
    check(near(maxX - minX, kPieceWidth) && near(maxY - minY, kPieceDepth), "facing +Y: width along X, depth along Y");

    const auto square = hull({{0, 0}, {2, 0}, {2, 2}, {0, 2}, {1, 1}, {1, 0}});
    check(square.size() == 4, "hull drops inner and edge points");
    check(hull({{0, 0}, {1, 1}}).size() == 2, "two points come back as they are");

    std::printf(g_failures ? "FAILED\n" : "PASS\n");
    return g_failures ? 1 : 0;
}
