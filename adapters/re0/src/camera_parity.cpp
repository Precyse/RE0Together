#include "camera_parity.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>

#include "character_owner.h"
#include "command_log.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "room_phase.h"
#include "log.h"
#include "net_pad.h"
#include "state_sync.h"

namespace {

using character_owner::Character;
using Clock = std::chrono::steady_clock;

constexpr auto kMinSwapInterval = std::chrono::seconds(1);
// After a guest's own switch, parity waits this long for the host to apply the request before reverting it.
constexpr auto kRequestGrace = std::chrono::milliseconds(1500);
// The game's own V may fire as well as the adapter's; a game switch this soon after ours would undo it.
constexpr auto kSwitchDedupe = std::chrono::milliseconds(500);
constexpr Character kHostCharacter = Character::Rebecca;

NetClient* g_net = nullptr;
std::atomic<Character> g_hostCharacter{Character::Unknown};  // guest: the host's controlled character
std::atomic<Character> g_requested{Character::Unknown};      // host: the character to switch to
Clock::time_point g_lastSwap;
std::atomic<Clock::time_point> g_graceUntil{};  // written by the net thread (local press), read on the game thread
uintptr_t g_seenLow = 0;                    // host: the two player objects at the last check, ordered
uintptr_t g_seenHigh = 0;
Character g_focus = Character::Unknown;     // host: focused character after our last switch or accepted change
Clock::time_point g_lastAdapterSwitch;

Character otherThan(Character character) {
    return character == Character::Billy ? Character::Rebecca : Character::Billy;
}

enum class SwitchResult { Done, AlreadyFocused, NoPartner, PartnerIsOther };

const char* reasonOf(SwitchResult result) {
    switch (result) {
        case SwitchResult::AlreadyFocused: return "focus already set";
        case SwitchResult::NoPartner: return "no partner object";
        case SwitchResult::PartnerIsOther: return "partner is not the target";
        case SwitchResult::Done: break;
    }
    return "done";
}

// Makes `wanted` the controlled character when it is currently the partner.
SwitchResult switchTo(Character wanted) {
    const uintptr_t controlled = game::controlled();
    const uintptr_t partner = game::partner();
    if (!controlled || !partner) return SwitchResult::NoPartner;
    if (character_owner::identify(controlled) == wanted) return SwitchResult::AlreadyFocused;
    if (character_owner::identify(partner) != wanted) return SwitchResult::PartnerIsOther;
    // Apart, the switch is the game's own zap, which also loads the partner's room.
    if (!game_state::inCurrentRoom(partner)) game::requestRoomPhase(room_phase::Change);
    else game::swapControlled(partner, controlled);
    return SwitchResult::Done;
}

void hostSwitchTo(Character wanted, Clock::time_point now) {
    const SwitchResult result = switchTo(wanted);
    if (result != SwitchResult::Done) {
        command_log::decide("switch to %s ignored: %s", character_owner::name(wanted), reasonOf(result));
        return;
    }
    g_focus = wanted;
    g_lastAdapterSwitch = now;
    command_log::decide("switch to %s applied", character_owner::name(wanted));
}

// The one place a switch request enters the host, from its own key and from a guest's SWITCH_REQUEST.
void queueSwitch(Character target, const char* origin) {
    command_log::note("switch to %s requested by %s", character_owner::name(target), origin);
    g_requested = target;
}

// The game's own V changes the focus too. Right after an adapter switch it is a duplicate that would toggle
// back, so it is undone; otherwise it is accepted.
void watchGameSwitch(Character current, Clock::time_point now) {
    const bool duplicate = g_focus != Character::Unknown && current != g_focus && current != Character::Unknown &&
                           now - g_lastAdapterSwitch < kSwitchDedupe;
    if (duplicate) {
        switchTo(g_focus);
        command_log::note("undid a duplicate game switch");
        return;
    }
    g_focus = current;
}

// Fixed ownership: the host's character is Rebecca. Whenever the player objects change (session start, save
// load), focus goes to her once; the guest's camera then mirrors it.
bool focusHostCharacterOnNewObjects(uintptr_t controlled, uintptr_t partner, Character current, Clock::time_point now) {
    const uintptr_t low = std::min(controlled, partner);
    const uintptr_t high = std::max(controlled, partner);
    if (low == g_seenLow && high == g_seenHigh) return false;
    g_seenLow = low;
    g_seenHigh = high;
    g_focus = current;
    if (current != kHostCharacter) hostSwitchTo(kHostCharacter, now);
    return true;
}

void forgetHostState() {
    g_seenLow = g_seenHigh = 0;
    g_focus = Character::Unknown;
    g_requested = Character::Unknown;
}

// Why the host cannot act on the players right now, or null.
const char* hostBlocker(uintptr_t controlled, uintptr_t partner) {
    if (!net_pad::active()) return "no peer connected";
    if (!controlled || !partner) return "no partner object";
    if (game_state::doorActive()) return "door active";
    return nullptr;
}

void hostTick() {
    const uintptr_t controlled = game::controlled();
    const uintptr_t partner = game::partner();
    const Character requested = g_requested.exchange(Character::Unknown);
    if (const char* blocker = hostBlocker(controlled, partner)) {
        if (!net_pad::active()) forgetHostState();
        if (requested != Character::Unknown) {
            command_log::decide("switch to %s ignored: %s", character_owner::name(requested), blocker);
        }
        return;
    }
    const auto now = Clock::now();
    const Character current = character_owner::identify(controlled);
    if (!focusHostCharacterOnNewObjects(controlled, partner, current, now)) watchGameSwitch(current, now);
    if (requested != Character::Unknown) hostSwitchTo(requested, now);
}

void guestTick() {
    const auto now = Clock::now();
    const Character wanted = g_hostCharacter;
    if (wanted == Character::Unknown || game_state::menuOpen() || now < g_graceUntil.load() || now - g_lastSwap < kMinSwapInterval) return;
    if (switchTo(wanted) != SwitchResult::Done) return;
    g_lastSwap = now;
    logger::write("camera_parity: swapped controlled character to %u to match the host", static_cast<unsigned>(wanted));
}

void onTick() {
    if (!character_owner::isHost()) {
        if (g_net) guestTick();
        return;
    }
    hostTick();
}

}  // namespace

namespace camera_parity {

void onFrame(const GameFrame& frame) {
    if (frame.type == kMsgSwitchRequest) {
        if (frame.payload.size() != 1) return;
        const Character target = static_cast<Character>(frame.payload[0]);
        if (character_owner::isHost()) {
            command_log::note("SWITCH_REQUEST received from slot %u", frame.slot);
            queueSwitch(target, "guest");
        } else {
            command_log::decide("SWITCH_REQUEST from slot %u ignored: not the host", frame.slot);
        }
        return;
    }
    if (frame.type != state_sync::kMsgPlayerState || frame.payload.size() != sizeof(state_sync::PlayerState)) return;
    state_sync::PlayerState state;
    std::memcpy(&state, frame.payload.data(), sizeof(state));
    if (state.senderIsHost) g_hostCharacter = static_cast<Character>(state.focusedCharacterId);
}

void onLocalSwitchKey() {
    const Character focused = character_owner::identify(game::controlled());
    if (focused == Character::Unknown) return;
    const Character target = otherThan(focused);
    if (character_owner::isHost()) {
        queueSwitch(target, "host");
        return;
    }
    const uint8_t id = static_cast<uint8_t>(target);
    const bool sent = g_net && g_net->send(kMsgSwitchRequest, true, proto::kSlotAll, {&id, sizeof(id)});
    if (!sent) {
        command_log::decide("switch to %s not sent: link busy", character_owner::name(target));
        return;
    }
    g_graceUntil = Clock::now() + kRequestGrace;
    command_log::note("SWITCH_REQUEST to %s sent", character_owner::name(target));
}

void holdLocalFocus() {
    if (character_owner::isHost()) {
        g_focus = character_owner::identify(game::controlled());
        g_lastAdapterSwitch = Clock::now();
        return;
    }
    g_graceUntil = Clock::now() + kRequestGrace;
}

void enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("camera_parity", onTick);
}

}  // namespace camera_parity
