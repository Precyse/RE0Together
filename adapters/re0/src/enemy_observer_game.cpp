#include "enemy_observer_game.h"

#include <cmath>

#include "player_damage.h"
#include "pose_wire.h"

namespace {

constexpr size_t kBodyLayer = 0;
constexpr size_t kOverlayLayer = 1;

uintptr_t layerAddress(uintptr_t enemy, size_t layer) {
    return enemy + game::kMotionLayerBaseOffset + layer * game::kMotionLayerStride;
}

bool readLayer(uintptr_t enemy, size_t layer, uint16_t& motion, float& frame) {
    const uintptr_t base = layerAddress(enemy, layer);
    return game::readMemory(base + game::kMotionNumberOffset, motion) && game::readMemory(base + game::kMotionFrameOffset, frame);
}

void writeHitAttributes(uintptr_t enemy, uint16_t attributes) {
    uint32_t word = 0;
    if (!game::readMemory(enemy + game::kPlayerHitAttributesOffset, word)) return;
    const uint32_t next = (word & ~uint32_t{pose_wire::kHitAttributeMask}) | attributes;
    if (next != word) game::writeMemory(enemy + game::kPlayerHitAttributesOffset, next);
}

}  // namespace

namespace enemy_observer_game {

bool read(uintptr_t enemy, enemy_observer_wire::PoseEntry& entry) {
    int32_t hp = 0;
    float body = 0.0f;
    float overlay = 0.0f;
    float quat[4];
    uint32_t hitAttributes = 0;
    if (!game::readMemory(enemy + game::kEnemyHpOffset, hp) || !game::readTransform(enemy, entry.pos, quat) ||
        !readLayer(enemy, kBodyLayer, entry.motion0, body) || !readLayer(enemy, kOverlayLayer, entry.motion1, overlay) ||
        !game::readMemory(enemy + game::kEnemyActionOffset, entry.record) ||
        !game::readMemory(enemy + game::kPlayerHitAttributesOffset, hitAttributes) ||
        !game::readMemory(enemy + game::kEnemyFlags6a0cOffset, entry.flags6a0c) || !std::isfinite(body) || !std::isfinite(overlay)) {
        return false;
    }
    entry.hp = enemy_observer_wire::clampHp(hp);
    entry.quat = pose_wire::packQuat(quat);
    entry.frame0 = body;
    entry.frame1 = pose_wire::packFrame(overlay);
    entry.hitAttr = static_cast<uint16_t>(hitAttributes & pose_wire::kHitAttributeMask);
    return true;
}

bool recordState(uintptr_t enemy, int32_t& state) { return game::readMemory(enemy + game::kEnemyActionOffset, state); }

void place(uintptr_t enemy, const float (&pos)[3], const float (&quat)[4], int32_t hp) {
    game::writeTransform(enemy, pos, quat);
    int32_t current = 0;
    if (game::readMemory(enemy + game::kEnemyHpOffset, current) && current != hp) player_damage::setHp(enemy, hp);
}

void write(uintptr_t enemy, const enemy_observer_rule::Sample& shown) {
    place(enemy, shown.pos, shown.quat, shown.hp);
    if (enemy_observer_rule::recordApplies(shown.record)) game::writeMemory(enemy + game::kEnemyActionOffset, shown.record);
    writeHitAttributes(enemy, shown.hitAttr);
    const bool alive = (shown.flags & enemy_observer_wire::kFlagAlive) != 0 && shown.hp > 0;
    game::writeMemory(enemy + game::kEnemyFlags6a0cOffset, enemy_observer_rule::shownFlags6a0c(shown.flags6a0c, alive));
}

bool holdCharPause(CharPause& held) {
    const uintptr_t address = game::modeFlagsAddress();
    uint32_t flags = 0;
    if (!game::readMemory(address, flags)) return false;
    held.wasSet = (flags & game::kCharPauseBit) != 0;
    if (!game::writeMemory(address, flags | game::kCharPauseBit)) return false;
    held.address = address;
    return true;
}

void releaseCharPause(CharPause& held) {
    uint32_t flags = 0;
    if (held.address && !held.wasSet && game::readMemory(held.address, flags)) {
        game::writeMemory(held.address, flags & ~game::kCharPauseBit);
    }
    held.address = 0;
}

}  // namespace enemy_observer_game
