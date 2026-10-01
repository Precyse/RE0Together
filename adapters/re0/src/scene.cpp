#include "scene.h"

#include <windows.h>

#include "game.h"
#include "log.h"

namespace {

uintptr_t sceneInfo() { return game::readPointer(game::kSceneInfoGlobal); }

uint16_t idOf(uintptr_t record) {
    uint32_t id = scene::kNone;
    return record && game::readMemory(record + game::kSceneRecordIdOffset, id) ? static_cast<uint16_t>(id) : scene::kNone;
}

uintptr_t recordOf(uintptr_t player) { return player ? game::readPointer(player + game::kPlayerSceneRecordOffset) : 0; }

uintptr_t currentRecord() {
    const uintptr_t info = sceneInfo();
    return info ? game::readPointer(info + game::kSceneCurrentRecordOffset) : 0;
}

// Tells the per-room unit registry which room the player's units belong to (kNoScene detaches them).
void registerUnits(uintptr_t player, int32_t sceneId) {
    void* registry = reinterpret_cast<void*>(game::readPointer(game::kUnitRoomRegistryGlobal));
    const uint32_t handle = game::readPointer(player + game::kPlayerUnitHandleOffset);
    if (registry) game::callThiscall<void>(game::kUnitRoomRegistryFunction, registry, sceneId, handle);
}

// True when neither character belongs to `record`.
bool empty(uintptr_t record) { return recordOf(game::controlled()) != record && recordOf(game::partner()) != record; }

// The door carry's sequence for one character (0x61e2c0): detach its units, find or load the room's record, place
// it on the entry spot, assign it, attach its units to the new room; then the old record goes if it is dormant and
// empty.
void moveUnguarded(uintptr_t player, uint16_t sceneId, uint32_t entry) {
    void* info = reinterpret_cast<void*>(sceneInfo());
    const uintptr_t old = recordOf(player);
    if (!info || !old) return;
    const uint32_t flags = game::readPointer(game::kSceneDefaultFlagsGlobal) | game::kSceneDoorFlag;
    registerUnits(player, game::kNoScene);
    auto* record = game::callThiscall<void*>(game::kSceneRecordFunction, info, static_cast<uint32_t>(sceneId), flags);
    if (!record) return;
    game::callThiscall<void>(game::kScenePlaceFunction, record, reinterpret_cast<void*>(player), entry,
                             game::kScenePlaceDoorMode);
    game::callThiscall<void>(game::kSceneAssignFunction, info, record, reinterpret_cast<void*>(player));
    registerUnits(player, static_cast<int32_t>(idOf(reinterpret_cast<uintptr_t>(record))));
    if (old != currentRecord() && old != recordOf(player) && empty(old)) {
        game::callThiscall<void>(game::kSceneReleaseFunction, info, reinterpret_cast<void*>(old));
    }
}

bool moveGuarded(uintptr_t player, uint16_t sceneId, uint32_t entry) {
    __try {
        moveUnguarded(player, sceneId, entry);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

namespace scene {

uint16_t of(uintptr_t player) { return idOf(recordOf(player)); }

uint16_t current() { return idOf(currentRecord()); }

bool move(uintptr_t player, uint16_t sceneId, uint32_t entry) {
    if (!moveGuarded(player, sceneId, entry)) {
        logger::write("scene: moving 0x%x to scene 0x%x faulted", static_cast<unsigned>(player), sceneId);
        return false;
    }
    logger::write("scene: 0x%x now in scene 0x%x entry %u (loaded scene 0x%x)", static_cast<unsigned>(player),
                  of(player), entry, current());
    return true;
}

}  // namespace scene
