#pragma once
// Pure geometry for drawing a partner's load (no game, unit tested): where each carried piece sits on the back of a
// body (two wide, bottom up), the corners of its box in the world, and the screen outline of a projected box.
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "world_to_screen.h"

namespace load_shape {

struct Box {
    world_to_screen::Vec3 centre;
    double halfWidth, halfDepth, halfHeight;  // metres, along the body's right, forward and up
    float yaw;                                // the body's heading, radians about Z (forward = (sin, cos))
};

constexpr double kPieceWidth = 0.30, kPieceDepth = 0.28, kPieceHeight = 0.24;  // a typical cargo case
constexpr int kColumns = 2;            // pieces side by side across the back
constexpr double kStackBase = 0.95;    // metres above the feet: the bottom of the load, the small of the back
constexpr double kStackBehind = 0.34;  // metres behind the body's centre
constexpr int kMaxShown = 12;          // a taller stack is drawn at this height

// The boxes of `pieces` carried pieces, stacked two wide up the back of a body standing at `feet` and facing `yaw`.
inline std::vector<Box> stack(const world_to_screen::Vec3& feet, float yaw, int pieces) {
    const double fx = std::sin(yaw), fy = std::cos(yaw);  // forward
    const double rx = fy, ry = -fx;                        // right
    std::vector<Box> boxes;
    for (int i = 0; i < std::min(pieces, kMaxShown); ++i) {
        const double side = (i % kColumns - (kColumns - 1) * 0.5) * kPieceWidth;
        const double z = feet.z + kStackBase + (i / kColumns + 0.5) * kPieceHeight;
        boxes.push_back({{feet.x - fx * kStackBehind + rx * side, feet.y - fy * kStackBehind + ry * side, z},
                         kPieceWidth / 2,
                         kPieceDepth / 2,
                         kPieceHeight / 2,
                         yaw});
    }
    return boxes;
}

inline std::array<world_to_screen::Vec3, 8> corners(const Box& box) {
    const double fx = std::sin(box.yaw), fy = std::cos(box.yaw);  // forward
    const double rx = fy, ry = -fx;                                // right
    std::array<world_to_screen::Vec3, 8> out{};
    int n = 0;
    for (int sr : {-1, 1}) {
        for (int sf : {-1, 1}) {
            for (int su : {-1, 1}) {
                out[n++] = {box.centre.x + sr * box.halfWidth * rx + sf * box.halfDepth * fx,
                            box.centre.y + sr * box.halfWidth * ry + sf * box.halfDepth * fy,
                            box.centre.z + su * box.halfHeight};
            }
        }
    }
    return out;
}

struct Point {
    float x, y;
};

// Convex hull of screen points, counter-clockwise in screen space (monotone chain); fewer than 3 points come back as
// they are.
inline std::vector<Point> hull(std::vector<Point> points) {
    if (points.size() < 3) return points;
    std::sort(points.begin(), points.end(),
              [](const Point& a, const Point& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    const auto cross = [](const Point& o, const Point& a, const Point& b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };
    std::vector<Point> out(points.size() * 2);
    size_t k = 0;
    for (const Point& p : points) {
        while (k >= 2 && cross(out[k - 2], out[k - 1], p) <= 0) --k;
        out[k++] = p;
    }
    for (size_t i = points.size() - 1, lower = k + 1; i-- > 0;) {
        while (k >= lower && cross(out[k - 2], out[k - 1], points[i]) <= 0) --k;
        out[k++] = points[i];
    }
    out.resize(k - 1);
    return out;
}

}  // namespace load_shape
