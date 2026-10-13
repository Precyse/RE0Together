#include "peer_pose.h"

#include <array>
#include <map>
#include <mutex>

#include "ds2/sim_tick.h"
#include "player_sync.h"
#include "position_blend.h"
#include "remote_body.h"

namespace {

constexpr float kSnapMetres = 10.0f;  // a jump this large (teleport, respawn) moves the pose at once

using Position = std::array<float, 3>;

std::mutex g_mutex;
std::map<uint8_t, Position> g_shown;  // per peer slot: the smoothed position, written by the simulation tick

// Moves the shown position part of the way toward the peer's extrapolated position. Caller holds g_mutex.
world_to_screen::Vec3 smoothed(const player_sync::RemotePlayer& peer) {
    auto [it, fresh] = g_shown.try_emplace(peer.slot, Position{peer.position[0], peer.position[1], peer.position[2]});
    float shown[3] = {it->second[0], it->second[1], it->second[2]};
    if (!fresh && position_blend::distance(shown, peer.position) <= kSnapMetres) {
        float blended[3];
        position_blend::blendPosition(shown, peer.position, blended);
        it->second = {blended[0], blended[1], blended[2]};
    } else {
        it->second = {peer.position[0], peer.position[1], peer.position[2]};
    }
    return {it->second[0], it->second[1], it->second[2]};
}

void tick() {
    for (const player_sync::RemotePlayer& peer : player_sync::remotePlayers()) {
        world_to_screen::Vec3 at;
        {
            std::lock_guard lock(g_mutex);
            at = smoothed(peer);
        }
        remote_body::setTarget(peer.slot, {at, peer.yaw}, {peer.velocity[0], peer.velocity[1], peer.velocity[2]},
                              partner_status::away(peer.status));
    }
}

}  // namespace

namespace peer_pose {

void installEarly() { sim_tick::add(&tick, "peer pose"); }

std::optional<world_to_screen::Vec3> shown(uint8_t slot) {
    std::lock_guard lock(g_mutex);
    const auto it = g_shown.find(slot);
    if (it == g_shown.end()) return std::nullopt;
    return world_to_screen::Vec3{it->second[0], it->second[1], it->second[2]};
}

}  // namespace peer_pose
