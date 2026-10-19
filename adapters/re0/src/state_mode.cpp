#include "state_mode.h"

#include <atomic>

#include "game_tick.h"
#include "log.h"
#include "pose_capture.h"
#include "puppet.h"
#include "room_authority.h"
#include "scene.h"
#include "state_net.h"

namespace {

constexpr unsigned kModuleCount = 4;  // load serial observer, pose capture, puppet, room authority

std::atomic<bool> g_on{false};

}  // namespace

namespace state_mode {

void configure(PartnerMode mode) { g_on = mode == PartnerMode::State; }

bool on() { return g_on.load(std::memory_order_relaxed); }

void enableAll(NetClient& net) {
    state_net::bind(net);
    game_tick::addCallback("load_serial", scene::observeLoad);
    unsigned enabled = 1;
    // The puppet needs the cAnimControl::set hook that pose_capture installs.
    if (pose_capture::enable()) {
        ++enabled;
        if (puppet::enable()) ++enabled;
    }
    room_authority::enable();
    ++enabled;
    logger::write("state_mode: on, %u modules enabled", enabled);
    if (enabled < kModuleCount) logger::write("adapter: state mode unavailable");
}

}  // namespace state_mode
