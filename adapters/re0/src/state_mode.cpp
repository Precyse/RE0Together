#include "state_mode.h"

#include <atomic>

#include "boss_handover.h"
#include "contact_net.h"
#include "demo_skip.h"
#include "door_follow.h"
#include "enemy_lease.h"
#include "enemy_observer.h"
#include "flag_sequencer.h"
#include "game_tick.h"
#include "item_ledger.h"
#include "log.h"
#include "pose_capture.h"
#include "prop_sync.h"
#include "puppet.h"
#include "room_authority.h"
#include "room_move.h"
#include "scene.h"
#include "slow_frame.h"
#include "state_net.h"
#include "weapon_fx_capture.h"
#include "weapon_fx_replay.h"

namespace {

constexpr unsigned kModuleCount = 16;  // load serial observer, pose capture, puppet, room authority, flag sequencer, weapon fx capture, weapon fx replay, contact net, enemy lease, enemy observer, item ledger, room move, door follow, demo skip, prop sync, boss hand-over

std::atomic<bool> g_on{false};

}  // namespace

namespace state_mode {

void configure(PartnerMode mode) { g_on = mode == PartnerMode::State; }

bool on() { return g_on.load(std::memory_order_relaxed); }

void enableAll(NetClient& net) {
    state_net::bind(net);
    slow_frame::enable();
    game_tick::addCallback("load_serial", scene::observeLoad);
    unsigned enabled = 1;
    // The puppet needs the cAnimControl::set hook that pose_capture installs.
    if (pose_capture::enable()) {
        ++enabled;
        if (puppet::enable()) ++enabled;
    }
    room_authority::enable();
    ++enabled;
    if (flag_sequencer::enable()) ++enabled;
    if (weapon_fx_capture::enable()) ++enabled;
    if (weapon_fx_replay::enable()) ++enabled;
    // The contact and lease ticks run before the enemy observer's, so a lease granted or started in a frame scopes or frees
    // its enemy in the same frame.
    if (contact_net::enable()) ++enabled;
    if (enemy_lease::enable()) ++enabled;
    if (enemy_observer::enable()) ++enabled;
    if (item_ledger::enable()) ++enabled;
    if (room_move::enable()) ++enabled;
    if (door_follow::enable()) ++enabled;
    if (demo_skip::enable()) ++enabled;
    if (prop_sync::enable()) ++enabled;
    // After the enemy observer: a room that changes hands is seen after the observer's own hand-over.
    if (boss_handover::enable()) ++enabled;
    logger::write("state_mode: on, %u modules enabled", enabled);
    if (enabled < kModuleCount) logger::write("adapter: state mode unavailable");
}

}  // namespace state_mode
