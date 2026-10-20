#include "prop_game.h"

#include "game.h"
#include "pose_game.h"
#include "prop_wire.h"
#include "scene.h"

namespace {

// A row of the obstacle table: which registry unit a room script's obstacle word names.
struct UnitRow {
    uint8_t unitClass;
    uint8_t unitKind;
    uint16_t nth;
};
static_assert(sizeof(UnitRow) == game::kObstacleUnitRowSize);

bool pushing(uintptr_t player) {
    pose_wire::Pose think{};
    return pose_game::readThink(player, think) && think.group == game::kPushThinkGroup && think.state == game::kPushThinkState;
}

}  // namespace

namespace prop_game {

uintptr_t obstacleOf(uint16_t sceneId, uint8_t unit) {
    UnitRow row{};
    if (unit >= prop_wire::kUnitCount ||
        !game::readMemory(game::kObstacleUnitTable + unit * game::kObstacleUnitRowSize, row)) {
        return 0;
    }
    const auto index = scene::unitIndex(sceneId, row.unitClass, row.unitKind, row.nth);
    return index ? scene::unitObject(*index) : 0;
}

std::optional<Push> pushOf(uintptr_t player, uint16_t sceneId) {
    if (!pushing(player)) return std::nullopt;
    const uintptr_t obstacle = game::readPointer(player + game::kPlayerPushedObstacleOffset);
    if (!obstacle) return std::nullopt;
    for (uint8_t unit = 0; unit < prop_wire::kUnitCount; ++unit) {
        if (obstacleOf(sceneId, unit) == obstacle) return Push{unit, obstacle};
    }
    return std::nullopt;
}

bool readPose(uintptr_t obstacle, prop_rule::Pose& out) {
    float pos[3];
    float quat[4];
    if (!game::readTransform(obstacle, pos, quat)) return false;
    out.pos = {pos[0], pos[1], pos[2]};
    out.yaw = prop_rule::yawOf(quat);
    return true;
}

void writePose(uintptr_t obstacle, const prop_rule::Pose& pose) {
    const float position[3] = {pose.pos[0], pose.pos[1], pose.pos[2]};
    game::writeMemory(obstacle + game::kPlayerPositionOffset, position);
    game::writeMemory(obstacle + game::kObstacleFourthWordOffset, 0.0f);
    float quat[4];
    float current[3];
    if (!game::readTransform(obstacle, current, quat) || prop_rule::yawGap(prop_rule::yawOf(quat), pose.yaw) < prop_rule::kYawEpsilon) {
        return;
    }
    prop_rule::quatOf(pose.yaw, quat);
    game::writeMemory(obstacle + game::kPlayerRotationOffset, quat);
}

}  // namespace prop_game
