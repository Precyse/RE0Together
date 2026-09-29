#include "game_state.h"

#include "door_phase.h"
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

bool uiPausesWorld() { return menuOpen() || room_phase::pausesWorld(roomPhase()); }

uint16_t currentRoom() {
    const uint8_t stage = singletonByte(game::kGameInfoGlobal, game::kGameInfoStageOffset, 0);
    const uint8_t room = singletonByte(game::kGameInfoGlobal, game::kGameInfoRoomOffset, 0);
    return static_cast<uint16_t>(stage << kByteBits | room);
}

bool inCurrentRoom(uintptr_t player) {
    uint32_t flags = 0;
    return player && game::readMemory(player + game::kPlayerFlagsOffset, flags) &&
           (flags & game::kPlayerInCurrentRoomFlag) != 0;
}

}  // namespace game_state
