#include "split_rooms.h"

#include <atomic>
#include <chrono>
#include <optional>

#include "character_owner.h"
#include "door_travel.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "log.h"
#include "net_pad.h"
#include "party_mode.h"
#include "room_phase.h"

namespace {

using Clock = std::chrono::steady_clock;
using character_owner::Character;
using door_sync::DoorChange;

constexpr auto kStepTimeout = std::chrono::seconds(20);

enum class Step { Idle, FocusDoorCharacter, RunDoor, WaitDoor, FocusOwn };

std::optional<DoorChange> g_next;  // game thread only: the newest peer door not yet replayed
DoorChange g_current{};
std::atomic<Step> g_step{Step::Idle};  // written on the game thread, read by the net thread too
bool g_requested = false;  // the swap or zap for this step was issued
bool g_sawDoor = false;
Clock::time_point g_stepStart;

const char* stepName(Step step) {
    switch (step) {
        case Step::FocusDoorCharacter: return "focus door character";
        case Step::RunDoor: return "run door";
        case Step::WaitDoor: return "wait door";
        case Step::FocusOwn: return "focus own character";
        case Step::Idle: break;
    }
    return "idle";
}

void enter(Step step) {
    g_step = step;
    g_requested = false;
    g_sawDoor = false;
    g_stepStart = Clock::now();
}

bool together() {
    uint16_t peerRoom = 0;
    return door_travel::peerRoom(peerRoom) && peerRoom == game_state::currentRoom();
}

bool settled() {
    return !game_state::doorActive() && game_state::roomPhase() == room_phase::Main &&
           game_state::currentRoom() != game_state::kRoomLoading;
}

// Makes `character` the camera character (character_owner::switchTo, issued once per step). True once done.
bool focusStep(Character character) {
    if (character_owner::identify(game::controlled()) == character) return true;
    if (!g_requested) g_requested = character_owner::switchTo(character) == character_owner::SwitchResult::Done;
    return false;
}

void onTick() {
    if (!net_pad::active()) {
        g_next.reset();
        g_step = Step::Idle;
        return;
    }
    if (g_step == Step::Idle) {
        if (!g_next || !settled()) return;
        g_current = *g_next;
        g_next.reset();
        logger::write("split_rooms: replaying the peer's door to room 0x%x", g_current.room);
        enter(Step::FocusDoorCharacter);
    }
    if (Clock::now() - g_stepStart > kStepTimeout) {
        logger::write("split_rooms: step '%s' timed out, focusing the local character", stepName(g_step));
        enter(g_step == Step::FocusOwn ? Step::Idle : Step::FocusOwn);
        return;
    }
    switch (g_step) {
        case Step::FocusDoorCharacter:
            if (settled() && focusStep(static_cast<Character>(g_current.characterId))) enter(Step::RunDoor);
            break;
        case Step::RunDoor:
            if (!settled()) break;
            door_sync::run(g_current);
            enter(Step::WaitDoor);
            break;
        case Step::WaitDoor:
            if (game_state::doorActive()) g_sawDoor = true;
            else if (g_sawDoor && settled()) enter(Step::FocusOwn);
            break;
        case Step::FocusOwn:
            if (settled() && focusStep(character_owner::localCharacter())) {
                logger::write("split_rooms: door replayed, back on the local character");
                enter(Step::Idle);
            }
            break;
        case Step::Idle:
            break;
    }
}

}  // namespace

namespace split_rooms {

bool takeOver(const DoorChange& change) {
    // Together in TEAM both characters go through.
    if (together() && party_mode::current() == control_rule::PartyMode::Team) return false;
    g_next = change;
    return true;
}

bool apart() { return net_pad::active() && (replaying() || !together()); }

bool independent() { return apart() || party_mode::current() == control_rule::PartyMode::LeaveBehind; }

bool replaying() { return g_step != Step::Idle; }

bool localEnemyAuthority() { return character_owner::isHost() || apart(); }

void enable() { game_tick::addCallback("split_rooms", onTick); }

}  // namespace split_rooms
