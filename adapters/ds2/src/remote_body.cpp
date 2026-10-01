#include "remote_body.h"

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>

#include "log.h"
#include "vehicle_sync.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kReleaseAfter = std::chrono::seconds(3);  // no new target this long = the peer is gone

enum class State { Pending, Live, Failed };

struct Puppet {
    State state = State::Pending;
    game::Body body = 0;
    game::Pose home;  // where the borrowed NPC stood, restored on release
    game::Pose target;
    world_to_screen::Vec3 velocity;
    Clock::time_point targetAt;
    bool parked = false;  // put back home while the peer drives
};

// Puts a borrowed NPC back where it was found.
void release(uint8_t slot, Puppet& puppet) {
    game::placeBody(puppet.body, puppet.home, {});
    logger::write("body: slot %u released its body", slot);
    puppet = Puppet{};
}

std::atomic<bool> g_enabled{false};
std::mutex g_mutex;  // guards g_puppets (render thread writes targets, main thread spawns and moves)
std::map<uint8_t, Puppet> g_puppets;

}  // namespace

namespace remote_body {

void setEnabled(bool enabled) { g_enabled = enabled; }

void setTarget(uint8_t slot, const game::Pose& pose, const world_to_screen::Vec3& velocity) {
    if (!g_enabled.load()) return;
    std::lock_guard lock(g_mutex);
    Puppet& puppet = g_puppets[slot];
    puppet.target = pose;
    puppet.velocity = velocity;
    puppet.targetAt = Clock::now();
}

void tick() {
    if (!g_enabled.load()) return;
    std::lock_guard lock(g_mutex);
    const auto now = Clock::now();
    for (auto it = g_puppets.begin(); it != g_puppets.end();) {
        auto& [slot, puppet] = *it;
        if (now - puppet.targetAt > kReleaseAfter) {
            if (puppet.state == State::Live) release(slot, puppet);
            it = g_puppets.erase(it);
            continue;
        }
        if (puppet.state == State::Pending) {
            const auto borrowed = game::borrowBody();
            if (!borrowed) {
                ++it;
                continue;
            }
            puppet.body = *borrowed;
            const auto home = game::bodyPose(puppet.body);
            puppet.state = home ? State::Live : State::Failed;
            if (home) puppet.home = *home;
            else logger::write("body: no body for slot %u, marker only", slot);
        }
        const bool driving = vehicle_sync::isDriving(slot);
        if (puppet.state == State::Live && driving != puppet.parked) {
            puppet.parked = driving;
            if (driving) game::placeBody(puppet.body, puppet.home, {});
            logger::write("body: slot %u %s", slot,
                          driving ? "drives, body put back" : "left the vehicle, body follows");
        }
        if (puppet.state == State::Live && !puppet.parked &&
            !game::placeBody(puppet.body, puppet.target, puppet.velocity)) {
            logger::write("body: moving the body of slot %u faulted, marker only", slot);
            puppet.state = State::Failed;
        }
        ++it;
    }
}

}  // namespace remote_body
