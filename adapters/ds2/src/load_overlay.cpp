#include "load_overlay.h"

#include <vector>

#include "load_shape.h"

namespace {

constexpr ImU32 kFill = IM_COL32(150, 146, 136, 235);
constexpr ImU32 kEdge = IM_COL32(25, 25, 25, 255);
constexpr float kEdgeWidth = 1.5f;

}  // namespace

namespace load_overlay {

void draw(ImDrawList* list, const world_to_screen::Camera& camera, const world_to_screen::Vec3& feet, float yaw,
          int pieces, float width, float height) {
    for (const load_shape::Box& box : load_shape::stack(feet, yaw, pieces)) {
        const auto corners = load_shape::corners(box);
        std::vector<load_shape::Point> points;
        for (const world_to_screen::Vec3& corner : corners) {
            const auto p = world_to_screen::project(camera, corner, width, height);
            if (p) points.push_back({p->x, p->y});
        }
        if (points.size() != corners.size()) continue;  // partly behind the camera: skip rather than draw it inside out
        std::vector<ImVec2> polygon;
        for (const load_shape::Point& p : load_shape::hull(points)) polygon.emplace_back(p.x, p.y);
        list->AddConvexPolyFilled(polygon.data(), static_cast<int>(polygon.size()), kFill);
        list->AddPolyline(polygon.data(), static_cast<int>(polygon.size()), kEdge, ImDrawFlags_Closed, kEdgeWidth);
    }
}

}  // namespace load_overlay
