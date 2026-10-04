#pragma once
// DS2-internal: an entity's world transform as the enemy messages carry it (enemy_wire::Pose) and back.
#include <cstring>

#include "decima/world_transform.h"
#include "enemy_wire.h"

namespace enemy_pose {

inline enemy_wire::Pose toWire(const decima::WorldTransform& transform) {
    enemy_wire::Pose pose{};
    pose.position[0] = transform.position.x;
    pose.position[1] = transform.position.y;
    pose.position[2] = transform.position.z;
    std::memcpy(pose.rotation, transform.orientation.row, sizeof(pose.rotation));
    return pose;
}

inline decima::WorldTransform fromWire(const enemy_wire::Pose& pose) {
    decima::WorldTransform transform{};
    transform.position = {pose.position[0], pose.position[1], pose.position[2]};
    std::memcpy(transform.orientation.row, pose.rotation, sizeof(pose.rotation));
    return transform;
}

}  // namespace enemy_pose
