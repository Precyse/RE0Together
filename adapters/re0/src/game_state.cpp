#include "game_state.h"

#include "door_phase.h"
#include "room_move_rule.h"
#include "room_phase.h"
#include "game.h"

namespace {

constexpr unsigned kByteBits = 8;

// Low byte of the field at singleton + offset, or `fallback` when the singleton or field is unreadable.
uint8_t singletonByte(uintptr_t global, uintptr_t offset, uint8_t fallback) {
    const uintptr_t object = game::readPointer(global);
    uint8_t value = 0;
    return object && game::readMemory(object + offset, value) ? value : fallback;
}

}  // namespace

namespace game_state {

int32_t doorPhase() {
    const uintptr_t object = game::readPointer(game::kDoorLoadGlobal);
    int32_t phase = 0;
    return object && game::readMemory(object + game::kDoorLoadStateOffset, phase) ? phase : door_phase::kUnreadable;
}

bool doorActive() { return door_phase::running(doorPhase()); }

bool menuOpen() { return singletonByte(game::kSubMenuGlobal, game::kSubMenuStateOffset, game::kSubMenuClosed) != game::kSubMenuClosed; }

int32_t roomPhase() {
    const uintptr_t control = game::readPointer(game::kRoomControlGlobal);
    int32_t phase = room_phase::kUnreadable;
    return control && game::readMemory(control + game::kRoomPhaseCurrentOffset, phase) ? phase : room_phase::kUnreadable;
}

int32_t phaseBeneath() {
    const uintptr_t control = game::readPointer(game::kRoomControlGlobal);
    int32_t depth = 0;
    int32_t phase = room_phase::kUnreadable;
    if (!control || !game::readMemory(control + game::kRoomPhaseDepthOffset, depth)) return room_phase::kUnreadable;
    if (depth < 1 || depth > game::kRoomPhaseStackSize) return room_phase::kUnreadable;
    const uintptr_t top = control + game::kRoomPhaseStackOffset + static_cast<uintptr_t>(depth - 1) * sizeof(int32_t);
    return game::readMemory(top, phase) ? phase : room_phase::kUnreadable;
}

bool scriptEventPaused() {
    uint32_t flags = 0;
    return game::readMemory(game::modeFlagsAddress(), flags) && (flags & game::kScriptEventBits) != 0;
}

bool unitsStopped(uint32_t reason) {
    const uintptr_t units = game::readPointer(game::kUnitSystemGlobal);
    uint32_t mask = 0;
    return units && game::readMemory(units + game::kUnitStopMaskOffset, mask) && (mask >> reason & 1) != 0;
}

bool paused() { return unitsStopped(game::kStopReasonPause); }

bool playing() { return roomPhase() == room_phase::Main && !doorActive() && !menuOpen(); }

uint16_t currentRoom() {
    const uint8_t stage = singletonByte(game::kGameInfoGlobal, game::kGameInfoStageOffset, 0);
    const uint8_t room = singletonByte(game::kGameInfoGlobal, game::kGameInfoRoomOffset, 0);
    return static_cast<uint16_t>(stage << kByteBits | room);
}

bool inCurrentRoom(uintptr_t player) {
    uint32_t hiddenReasons = 0;
    const uintptr_t sceneInfo = game::readPointer(game::kSceneInfoGlobal);
    const uintptr_t loaded = sceneInfo ? game::readPointer(sceneInfo + game::kSceneCurrentRecordOffset) : 0;
    return player && loaded && game::readMemory(player + game::kPlayerHiddenReasonsOffset, hiddenReasons) &&
           room_move_rule::inLoadedRoom(game::readPointer(player + game::kPlayerSceneRecordOffset) == loaded, hiddenReasons);
}

}  // namespace game_state
