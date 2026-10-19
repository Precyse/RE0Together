#include "scene.h"

#include <windows.h>

#include <atomic>

#include "game.h"
#include "load_serial.h"
#include "log.h"
#include "spot_rule.h"

namespace {

load_serial::Tracker g_loadTracker;  // game thread only
std::atomic<uint16_t> g_loadSerial{load_serial::kFirstSerial};

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

// The mode (spot of the entry) a character is put on in `record`: the door spot, or a distinct one when the other
// character already stands there, so the two never share a spot (spot_rule.h).
uint32_t placeMode(uintptr_t record, uint32_t entry, bool otherInRoom) {
    spot_rule::Spot spots[spot_rule::kModes] = {};
    for (uint32_t mode = 0; mode < game::kScenePlaceModeCount; ++mode) {
        const uintptr_t address =
            record + game::kSceneEntrySpotsOffset + (entry * game::kScenePlaceModeCount + mode) * game::kSceneEntrySpotSize;
        if (!game::readMemory(address, spots[mode])) return game::kScenePlaceDoorMode;
    }
    return spot_rule::modeFor(otherInRoom, spots);
}

// True when the character other than `player` stands in `record`.
bool otherIn(uintptr_t player, uintptr_t record) {
    const uintptr_t other = player == game::controlled() ? game::partner() : game::controlled();
    return other && recordOf(other) == record;
}

// The door carry's sequence for one character (0x61e2c0): detach its units, find or load the room's record, place
// it on the entry spot, assign it, attach its units to the new room; then the old record goes if it is dormant and
// empty.
void moveUnguarded(uintptr_t player, uint16_t sceneId, uint32_t entry, std::optional<uint32_t> spotMode) {
    void* info = reinterpret_cast<void*>(sceneInfo());
    const uintptr_t old = recordOf(player);
    if (!info || !old) return;
    const uint32_t flags = game::readPointer(game::kSceneDefaultFlagsGlobal) | game::kSceneDoorFlag;
    registerUnits(player, game::kNoScene);
    auto* record = game::callThiscall<void*>(game::kSceneRecordFunction, info, static_cast<uint32_t>(sceneId), flags);
    if (!record) return;
    const auto recordAddress = reinterpret_cast<uintptr_t>(record);
    game::callThiscall<void>(game::kScenePlaceFunction, record, reinterpret_cast<void*>(player), entry,
                             spotMode ? *spotMode : placeMode(recordAddress, entry, otherIn(player, recordAddress)));
    game::callThiscall<void>(game::kSceneAssignFunction, info, record, reinterpret_cast<void*>(player));
    registerUnits(player, static_cast<int32_t>(idOf(reinterpret_cast<uintptr_t>(record))));
    if (old != currentRecord() && old != recordOf(player) && empty(old)) {
        game::callThiscall<void>(game::kSceneReleaseFunction, info, reinterpret_cast<void*>(old));
    }
}

bool moveGuarded(uintptr_t player, uint16_t sceneId, uint32_t entry, std::optional<uint32_t> spotMode) {
    __try {
        moveUnguarded(player, sceneId, entry, spotMode);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Places `player` on the follower spot of `entry` in its own record. False when the player has no record.
bool placeBehindUnguarded(uintptr_t player, uint32_t entry, uint32_t& mode) {
    const uintptr_t record = recordOf(player);
    if (!record) return false;
    mode = placeMode(record, entry, true);
    game::callThiscall<void>(game::kScenePlaceFunction, reinterpret_cast<void*>(record), reinterpret_cast<void*>(player),
                             entry, mode);
    return true;
}

bool placeBehindGuarded(uintptr_t player, uint32_t entry, uint32_t& mode) {
    __try {
        return placeBehindUnguarded(player, entry, mode);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

namespace scene {

uint16_t of(uintptr_t player) { return idOf(recordOf(player)); }

uint16_t current() { return idOf(currentRecord()); }

uint16_t loadSerial() { return g_loadSerial.load(std::memory_order_relaxed); }

void observeLoad() {
    const uintptr_t record = currentRecord();
    if (!g_loadTracker.observe(record, idOf(record))) return;
    g_loadSerial = g_loadTracker.value();
    logger::write("scene: load serial %u (scene 0x%02x)", g_loadTracker.value(), idOf(record));
}

bool move(uintptr_t player, uint16_t sceneId, uint32_t entry, std::optional<uint32_t> spotMode) {
    if (!moveGuarded(player, sceneId, entry, spotMode)) {
        logger::write("scene: moving 0x%x to scene 0x%x faulted", static_cast<unsigned>(player), sceneId);
        return false;
    }
    logger::write("scene: 0x%x now in scene 0x%x entry %u (loaded scene 0x%x)", static_cast<unsigned>(player),
                  of(player), entry, current());
    return true;
}

std::optional<uint32_t> placeBehind(uintptr_t player, uint32_t entry) {
    uint32_t mode = 0;
    if (!placeBehindGuarded(player, entry, mode)) {
        logger::write("scene: placing 0x%x behind at entry %u faulted", static_cast<unsigned>(player), entry);
        return std::nullopt;
    }
    logger::write("scene: 0x%x placed behind at entry %u spot %u", static_cast<unsigned>(player), entry, mode);
    return mode;
}

}  // namespace scene
