#include "state_mode.h"

#include <atomic>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

#include "boss_handover.h"
#include "contact_net.h"
#include "debug_overlay.h"
#include "demo_skip.h"
#include "door_follow.h"
#include "enemy_lease.h"
#include "enemy_observer.h"
#include "flag_sequencer.h"
#include "game_tick.h"
#include "item_ledger.h"
#include "join_extras.h"
#include "log.h"
#include "pose_capture.h"
#include "prop_sync.h"
#include "puppet.h"
#include "room_authority.h"
#include "room_move.h"
#include "scene.h"
#include "session_ready.h"
#include "slow_frame.h"
#include "state_net.h"
#include "weapon_fx_capture.h"
#include "weapon_fx_replay.h"

namespace {

constexpr float kIncompleteToastSeconds = 10.0f;
constexpr size_t kToastLength = 128;

std::atomic<bool> g_on{false};

struct Module {
    const char* name;
    bool (*enable)();
    bool needsPrevious;  // skipped, and counted as failed, when the module before it failed
};

bool enableLoadSerial() {
    game_tick::addCallback("load_serial", scene::observeLoad);
    return true;
}

bool enableRoomAuthority() {
    room_authority::enable();
    return true;
}

// Execution order within a frame is registration order. The puppet needs the cAnimControl::set hook that pose_capture
// installs. The contact and lease ticks run before the enemy observer's, so a lease granted or started in a frame scopes
// or frees its enemy in the same frame. The boss hand-over runs after the enemy observer: a room that changes hands is
// seen after the observer's own hand-over. The session handshake is last: it reads the claims, commits, ledger and flag
// serial that the modules above keep.
constexpr Module kModules[] = {
    {"load serial", enableLoadSerial, false},
    {"pose capture", pose_capture::enable, false},
    {"puppet", puppet::enable, true},
    {"room authority", enableRoomAuthority, false},
    {"flag sequencer", flag_sequencer::enable, false},
    {"weapon fx capture", weapon_fx_capture::enable, false},
    {"weapon fx replay", weapon_fx_replay::enable, false},
    {"contact net", contact_net::enable, false},
    {"enemy lease", enemy_lease::enable, false},
    {"enemy observer", enemy_observer::enable, false},
    {"item ledger", item_ledger::enable, false},
    {"room move", room_move::enable, false},
    {"door follow", door_follow::enable, false},
    {"demo skip", demo_skip::enable, false},
    {"prop sync", prop_sync::enable, false},
    {"boss hand-over", boss_handover::enable, false},
    {"join extras", join_extras::enable, false},
    {"session ready", session_ready::enable, false},
};

// Tells the player and the log which modules did not come up: the modules that did stay on, so the session runs without
// them (a missing puppet leaves the remote character with no driver, as no PAD_FRAME is sent in state mode).
void reportIncomplete(const std::vector<const char*>& failed) {
    std::string names;
    for (const char* name : failed) {
        logger::write("state_mode: module '%s' unavailable", name);
        names += (names.empty() ? "" : ", ");
        names += name;
    }
    logger::write("adapter: state mode incomplete, %zu of %zu modules unavailable", failed.size(), std::size(kModules));
    char text[kToastLength];
    snprintf(text, sizeof(text), "State mode incomplete: %s", names.c_str());
    debug_overlay::toast(text, kIncompleteToastSeconds);
}

}  // namespace

namespace state_mode {

void configure(PartnerMode mode) { g_on = mode == PartnerMode::State; }

bool on() { return g_on.load(std::memory_order_relaxed); }

void enableAll(NetClient& net) {
    state_net::bind(net);
    slow_frame::enable();
    std::vector<const char*> failed;
    bool previousUp = true;
    for (const Module& module : kModules) {
        previousUp = (previousUp || !module.needsPrevious) && module.enable();
        if (!previousUp) failed.push_back(module.name);
    }
    logger::write("state_mode: on, %zu modules enabled", std::size(kModules) - failed.size());
    if (!failed.empty()) reportIncomplete(failed);
}

}  // namespace state_mode
