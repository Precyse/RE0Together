#include "rescue.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <optional>

#include "character_owner.h"
#include "crash_dump.h"
#include "debug_overlay.h"
#include "demo_skip.h"
#include "door_sync.h"
#include "enemy_registry.h"
#include "event_sync.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "log.h"
#include "menu_mirror.h"
#include "pose_wire.h"
#include "rescue_rule.h"
#include "scene.h"

namespace {

namespace rule = rescue_rule;

constexpr float kToastSeconds = 2.5f;
constexpr uint32_t kMsPerSecond = 1000;

std::atomic<bool> g_pressQueued{false};
std::atomic<bool> g_menuHoldDropped{false};
rule::Ladder g_ladder;      // game thread only
rule::OrphanLock g_orphan;  // game thread only

uint32_t lockBits() {
    uint32_t flags = 0;
    game::readMemory(game::modeFlagsAddress(), flags);
    return flags & (game::kCutscenePauseBits | game::kEventPhaseBit);
}

// The own character's last door when it led into the loaded scene.
std::optional<door_sync::DoorChange> doorIntoScene(uint16_t scene) {
    const character_owner::Character own = character_owner::identify(character_owner::localPlayer());
    door_sync::DoorChange change{};
    if (own == character_owner::Character::Unknown || !door_sync::lastDoor(static_cast<uint8_t>(own), change) ||
        change.room != scene) {
        return std::nullopt;
    }
    return change;
}

// The engine's own end of an event (EventPhaseOut 0x56cd10): the two event bits of the mode flags cleared, then the room
// phase popped. Nothing is called when the pop sites are not the bytes the pop was read from.
bool callPop(void* control) {
    __try {
        game::callThiscall<void>(game::kRoomPopPhaseFunction, control);
        return true;
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "rescue: pop event")) {
        return false;
    }
}

bool popEvent() {
    void* const control = reinterpret_cast<void*>(game::readPointer(game::kRoomControlGlobal));
    const uintptr_t flagsAddress = game::modeFlagsAddress();
    uint32_t flags = 0;
    if (!control || !game::readMemory(flagsAddress, flags)) return false;
    if (!game::bytesMatch(game::kRoomPopPhaseFunction, game::kRoomPopPhaseBytes) ||
        !game::bytesMatch(game::kPhaseManagerPopFunction, game::kPhaseManagerPopBytes)) {
        logger::write("rescue: event phase not popped, unexpected bytes at the pop sites");
        return false;
    }
    game::writeMemory(flagsAddress, flags & ~(game::kPlayerPauseBit | game::kEventPhaseBit));
    return callPop(control);
}

rule::Release releasePlan() {
    const uintptr_t control = game::readPointer(game::kRoomControlGlobal);
    int32_t requested = 0;
    int32_t depth = 0;
    if (!control || !game::readMemory(control + game::kRoomPhaseRequestedOffset, requested) ||
        !game::readMemory(control + game::kRoomPhaseDepthOffset, depth)) {
        return rule::Release::Nothing;
    }
    return rule::releasePlan(game_state::roomPhase(), depth, requested, event_sync::scriptThreadActive());
}

// Step 0: frees what holds the world, each part only when its own conditions hold.
void release() {
    const unsigned followed = event_sync::releaseFollowed();
    const bool popped = releasePlan() == rule::Release::PopEvent && popEvent();
    const bool skipped = demo_skip::skipLocal();
    const uint32_t bits = lockBits();
    event_sync::releaseHeldPause(event_sync::PauseRelease::Rescue);
    logger::write("rescue: released (followed %u, event phase %s, demo %s, pause bits 0x%x, menu hold %s)", followed,
                  popped ? "popped" : "-", skipped ? "skipped" : "-", bits, g_menuHoldDropped.exchange(false) ? "dropped" : "-");
    debug_overlay::toast("Rescue: freed.", kToastSeconds, toast_queue::Style::Line);
}

// Step 1: the door that led into the room runs again for the own character.
void reload(const door_sync::DoorChange& change) {
    logger::write("rescue: reloading scene 0x%02x through entry %u", static_cast<unsigned>(change.room), change.entry);
    door_sync::runRescue(change);
    debug_overlay::toast("Room reloaded.", kToastSeconds, toast_queue::Style::Line);
}

void onPress() {
    const uint16_t scene = scene::current();
    rule::Conditions conditions;
    conditions.doorActive = game_state::doorActive();
    conditions.menuOpen = game_state::menuOpen();
    conditions.bossAlive = enemy_registry::bossAlive();
    const std::optional<door_sync::DoorChange> door = doorIntoScene(scene);
    conditions.hasDoor = door.has_value();
    const rule::Decision decision = g_ladder.press(pose_wire::clockMs(), scene, conditions);
    switch (decision.action) {
        case rule::Action::Release: return release();
        case rule::Action::Reload: return reload(*door);
        case rule::Action::Toast: break;
    }
    if (decision.refusal != rule::Refusal::None) {
        logger::write("rescue: reload refused (%s)", rule::name(decision.refusal));
    } else {
        logger::write("rescue: nothing left to try in scene 0x%02x", static_cast<unsigned>(scene));
    }
    debug_overlay::toast("Quit and rejoin.", kToastSeconds, toast_queue::Style::Panel);
}

// Pause bits no script holds are the engine's leftovers: released the same way the rescue frees them.
void watchOrphanLock() {
    const uint32_t bits = lockBits();
    if (!g_orphan.observe(pose_wire::clockMs(), bits, event_sync::scriptThreadActive(), game_state::playing())) return;
    logger::write("rescue: lock bits 0x%x held %u s with no script, released", bits, rule::kOrphanMs / kMsPerSecond);
    event_sync::releaseHeldPause(event_sync::PauseRelease::Rescue);
}

void onTick() {
    g_ladder.onScene(scene::current());
    if (g_pressQueued.exchange(false)) onPress();
    watchOrphanLock();
}

}  // namespace

namespace rescue {

void enable() { game_tick::addCallback("rescue", onTick); }

void press() {
    if (menu_mirror::releasePeerHold()) g_menuHoldDropped = true;
    g_pressQueued = true;
}

}  // namespace rescue
