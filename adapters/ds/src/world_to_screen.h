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
    double verticalFovRadians = 1.0;
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
    const double focal = (height * 0.5) / std::tan(camera.verticalFovRadians * 0.5);
    const double sx = width * 0.5 + dot(d, camera.right) / depth * focal;
    const double sy = height * 0.5 - dot(d, camera.up) / depth * focal;
    return ScreenPoint{static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(std::sqrt(dot(d, d)))};
}

}  // namespace world_to_screen
