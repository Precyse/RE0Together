#pragma once
// Pure camera projection (no game, unit tested): a world point to pixels through a pinhole camera.
#include <cmath>
#include <optional>

namespace world_to_screen {

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// The camera as the game places it: position plus unit right, up and forward axes in world space.
struct Camera {
    Vec3 position;
    Vec3 right;
    Vec3 up;
    Vec3 forward;
    double horizontalFovRadians = 1.0;  // Decima's CameraEntity.FOV is horizontal
    double nearPlane = 0.1;
};

struct ScreenPoint {
    float x = 0;
    float y = 0;
    float distance = 0;  // metres from the camera
};

// Pixels from the top-left corner, or nothing when the point is behind the near plane.
inline std::optional<ScreenPoint> project(const Camera& camera, const Vec3& world, float width, float height) {
    const Vec3 d = world - camera.position;
    const double depth = dot(d, camera.forward);
    if (depth < camera.nearPlane) return std::nullopt;
    const double focal = (width * 0.5) / std::tan(camera.horizontalFovRadians * 0.5);
    const double sx = width * 0.5 + dot(d, camera.right) / depth * focal;
    const double sy = height * 0.5 - dot(d, camera.up) / depth * focal;
    return ScreenPoint{static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(std::sqrt(dot(d, d)))};
}

// Where an off-screen point's arrow sits: on the screen edge `margin` pixels in, pointing the way to turn.
struct EdgeArrow {
    float x = 0;
    float y = 0;
    float angle = 0;  // radians of the pointing direction on screen, 0 = right, pi / 2 = down
};

constexpr double kDirectionEpsilon = 1e-6;

// Nothing while the point is on screen (inside the margin); behind the camera the arrow points the way the point lies
// to the sides, straight down when it is exactly behind.
inline std::optional<EdgeArrow> edgeArrow(const Camera& camera, const Vec3& world, float width, float height, float margin) {
    double dx = 0, dy = 0;
    if (const auto point = project(camera, world, width, height)) {
        if (point->x >= margin && point->x <= width - margin && point->y >= margin && point->y <= height - margin) {
            return std::nullopt;
        }
        dx = point->x - width * 0.5;
        dy = point->y - height * 0.5;
    } else {
        const Vec3 d = world - camera.position;
        dx = dot(d, camera.right);
        dy = -dot(d, camera.up);
    }
    if (std::fabs(dx) < kDirectionEpsilon && std::fabs(dy) < kDirectionEpsilon) dy = 1;
    const double halfWidth = width * 0.5 - margin, halfHeight = height * 0.5 - margin;
    const double toSide = std::fabs(dx) < kDirectionEpsilon ? HUGE_VAL : halfWidth / std::fabs(dx);
    const double toTop = std::fabs(dy) < kDirectionEpsilon ? HUGE_VAL : halfHeight / std::fabs(dy);
    const double scale = std::fmin(toSide, toTop);
    return EdgeArrow{static_cast<float>(width * 0.5 + dx * scale), static_cast<float>(height * 0.5 + dy * scale),
                     static_cast<float>(std::atan2(dy, dx))};
}

}  // namespace world_to_screen
