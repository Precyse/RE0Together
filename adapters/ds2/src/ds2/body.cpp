// DEATH STRANDING 2: moving a remote player's body to a pose: pushed with ds2::placeEntity, or teleported when it is
// far from the pose (a player mover writes a plain placement back, and after a ride its velocity no longer moves it).
#include <cmath>

#include "decima/world_transform.h"
#include "ds2/place.h"
#include "game.h"

namespace {

constexpr double kTeleportDistance = 2.0;  // metres

decima::WorldTransform transformOf(const game::Pose& pose) {
    const float s = std::sin(pose.yaw), c = std::cos(pose.yaw);
    decima::WorldTransform t{};
    t.position = {pose.position.x, pose.position.y, pose.position.z};
    t.orientation.row[0][0] = c;  // right
    t.orientation.row[0][1] = -s;
    t.orientation.row[1][0] = s;  // forward
    t.orientation.row[1][1] = c;
    t.orientation.row[2][2] = 1;  // up
    return t;
}

}  // namespace

namespace game {

bool placeBody(Body body, const Pose& pose, const world_to_screen::Vec3& velocity) {
    const decima::WorldTransform target = transformOf(pose);
    decima::WorldTransform now;
    if (ds2::entityTransform(body, now)) {
        const double dx = now.position.x - target.position.x, dy = now.position.y - target.position.y;
        if (std::sqrt(dx * dx + dy * dy) > kTeleportDistance) return ds2::teleportEntity(body, target);
    }
    return ds2::placeEntity(body, target, velocity);
}

}  // namespace game
