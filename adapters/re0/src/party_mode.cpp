#include "party_mode.h"

#include <atomic>
#include <chrono>

#include "character_owner.h"
#include "command_log.h"
#include "debug_overlay.h"
#include "debug_stats.h"
#include "game.h"
#include "game_tick.h"
#include "log.h"
#include "net_pad.h"

namespace {

using Clock = std::chrono::steady_clock;
using control_rule::PartyMode;

constexpr auto kAnnounceInterval = std::chrono::seconds(2);
constexpr float kToastSeconds = 3.0f;

NetClient* g_net = nullptr;
std::atomic<PartyMode> g_mode{PartyMode::Team};
std::atomic<bool> g_toggleRequested{false};  // host: a flip is pending (local key or guest request)
std::atomic<bool> g_announceToast{false};    // the mode changed and has not been toasted yet
Clock::time_point g_lastAnnounce;

const char* modeName(PartyMode mode) { return mode == PartyMode::Team ? "team" : "leave behind"; }

void setMode(PartyMode mode) {
    if (g_mode.exchange(mode) == mode) return;
    debug_stats::set(debug_stats::Gauge::PartyMode, static_cast<int>(mode));
    g_announceToast = true;
    logger::write("party_mode: %s", modeName(mode));
}

void announce() {
    const uint8_t mode = static_cast<uint8_t>(g_mode.load());
    g_lastAnnounce = Clock::now();
    g_net->send(proto::kMsgPartyMode, true, proto::kSlotAll, {&mode, sizeof(mode)});
}

void toastMode() { debug_overlay::toast(g_mode == PartyMode::Team ? "Team" : "Split up", kToastSeconds); }

// The game's own follow flag carries the partner through doors with the focused character, so it mirrors the
// mode: following in TEAM, staying in LEAVE_BEHIND. Written only when it differs.
void applyFollowFlag() {
    const uintptr_t player = game::readPointer(game::kPlayerGlobal);
    const uint8_t wanted = g_mode == PartyMode::Team ? 1 : 0;
    uint8_t current = 0;
    if (!player || !game::readMemory(player + game::kPlayerFollowOffset, current) || current == wanted) return;
    game::writeMemory(player + game::kPlayerFollowOffset, wanted);
}

// The one place a flip request enters the host, from its own key and from a guest's PARTY_REQUEST.
void queueToggle(const char* origin) {
    command_log::note("party toggle requested by %s", origin);
    g_toggleRequested = true;
}

void hostTick() {
    if (!g_toggleRequested.exchange(false)) {
        if (Clock::now() - g_lastAnnounce >= kAnnounceInterval) announce();
        return;
    }
    setMode(g_mode == PartyMode::Team ? PartyMode::LeaveBehind : PartyMode::Team);
    command_log::decide("party toggle applied: %s", modeName(g_mode));
    announce();
}

void onTick() {
    if (!net_pad::active()) {
        if (g_toggleRequested.exchange(false)) command_log::decide("party toggle ignored: no peer connected");
        setMode(PartyMode::Team);
        g_announceToast = false;
        return;
    }
    if (character_owner::isHost()) hostTick();
    applyFollowFlag();
    if (g_announceToast.exchange(false)) toastMode();
}

}  // namespace

namespace party_mode {

PartyMode current() { return g_mode; }

void onLocalToggleKey() {
    if (character_owner::isHost()) {
        queueToggle("host");
        return;
    }
    if (g_net && g_net->send(proto::kMsgPartyRequest, true, proto::kSlotAll, {})) {
        command_log::note("PARTY_REQUEST sent");
        return;
    }
    command_log::decide("party toggle not sent: link busy");
}

void setByScript(PartyMode mode) {
    if (!character_owner::isHost() || g_mode == mode) return;
    setMode(mode);
    command_log::decide("party mode set by a room script: %s", modeName(mode));
    announce();
}

void onFrame(const GameFrame& frame) {
    if (frame.type == proto::kMsgPartyRequest) {
        if (!frame.payload.empty()) return;
        if (character_owner::isHost()) {
            command_log::note("PARTY_REQUEST received from slot %u", frame.slot);
            queueToggle("guest");
        } else {
            command_log::decide("PARTY_REQUEST from slot %u ignored: not the host", frame.slot);
        }
        return;
    }
    if (frame.type != proto::kMsgPartyMode || frame.payload.size() != 1 || character_owner::isHost() ||
        !character_owner::isHostSlot(frame.slot)) {
        return;
    }
    setMode(static_cast<PartyMode>(frame.payload[0]));
}

void enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("party_mode", onTick);
}

}  // namespace party_mode
