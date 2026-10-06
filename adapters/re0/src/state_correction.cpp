#include "state_correction.h"

#include <chrono>
#include <cstring>
#include <mutex>

#include "character_owner.h"
#include "debug_stats.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "log.h"
#include "net_pad.h"
#include "player_damage.h"
#include "position_blend.h"
#include "room_phase.h"
#include "scene.h"
#include "state_sync.h"

namespace {

using Clock = std::chrono::steady_clock;
using position_blend::Action;

constexpr auto kLogInterval = std::chrono::seconds(1);
constexpr auto kForcedCheckWindow = std::chrono::seconds(5);
// Two states this many sends apart (or fewer) still give a usable velocity.
constexpr uint32_t kMaxVelocitySeqGap = 3;

struct Sample {
    state_sync::PlayerState state;
    Clock::time_point arrived;
};

std::mutex g_mutex;
Sample g_last{};         // guarded by g_mutex
Sample g_previous{};     // guarded by g_mutex
bool g_hasLast = false;  // guarded by g_mutex
bool g_hasPrevious = false;  // guarded by g_mutex
bool g_freshHp = false;  // guarded by g_mutex
Clock::time_point g_lastLog;
Clock::time_point g_forcedUntil;  // game thread only

// The owner's newest state, plus the one before it when it gives a usable velocity.
struct Reported {
    Sample last;
    bool hasVelocity;
    float velocity[3];
};

bool latestReport(Reported& out) {
    std::lock_guard lock(g_mutex);
    if (!g_hasLast) return false;
    out.last = g_last;
    out.hasVelocity = false;
    const uint32_t gap = g_last.state.seq - g_previous.state.seq;
    if (g_hasPrevious && gap > 0 && gap <= kMaxVelocitySeqGap && g_previous.state.room == g_last.state.room &&
        g_previous.state.characterId == g_last.state.characterId) {
        position_blend::velocity(g_previous.state.pos, g_last.state.pos, gap / state_sync::kSendHz, out.velocity);
        out.hasVelocity = true;
    }
    return true;
}

bool takeFreshHp(state_sync::PlayerState& out) {
    std::lock_guard lock(g_mutex);
    if (!g_freshHp) return false;
    out = g_last.state;
    g_freshHp = false;
    return true;
}

void applyHp(uintptr_t target, int32_t hp) {
    int32_t local = 0;
    if (!game::readMemory(target + game::kPlayerHpOffset, local) || local == hp) return;
    player_damage::setHp(target, hp);
}

void onTick() {
    state_sync::PlayerState remote;
    if (!takeFreshHp(remote)) return;
    const auto character = static_cast<character_owner::Character>(remote.characterId);
    if (!character_owner::isRemoteOwned(character)) return;
    const uintptr_t target = character_owner::find(character);
    if (target) applyHp(target, remote.hp);
}

void snap(uintptr_t target, const state_sync::PlayerState& remote, float drift) {
    game::writeTransform(target, remote.pos, remote.quat);
    debug_stats::count(debug_stats::Counter::Snaps);
    const auto now = Clock::now();
    if (now - g_lastLog < kLogInterval) return;
    g_lastLog = now;
    logger::write("state_correction: snapped character %u, drift=%.1f seq=%u", remote.characterId, drift, remote.seq);
}

void blend(uintptr_t target, const float (&pos)[3], const float (&quat)[4], const float (&goalPos)[3],
           const state_sync::PlayerState& remote) {
    float blendedPos[3];
    float blendedQuat[4];
    position_blend::blendPosition(pos, goalPos, blendedPos);
    position_blend::blendRotation(quat, remote.quat, blendedQuat);
    game::writeTransform(target, blendedPos, blendedQuat);
    debug_stats::count(debug_stats::Counter::Blends);
}

// After a character's own move (so the game's collision push from the local player is undone in the same frame):
// pull a remote-owned character toward its owner's reported position, extrapolated by the reported velocity.
void afterMove(uintptr_t player) {
    Reported report;
    if (!latestReport(report)) return;
    const state_sync::PlayerState& remote = report.last.state;
    const auto character = static_cast<character_owner::Character>(remote.characterId);
    if (character_owner::identify(player) != character || !character_owner::isRemoteOwned(character)) return;
    // Positions from another room are in unrelated coordinates; during a local event the script places it.
    if (room_phase::isEvent(game_state::roomPhase()) || remote.room != scene::current() || !game_state::inCurrentRoom(player) || game_state::doorActive()) {
        return;
    }
    float pos[3];
    float quat[4];
    if (!game::readTransform(player, pos, quat)) return;
    float goalPos[3];
    if (report.hasVelocity) {
        const float elapsed = std::chrono::duration<float>(Clock::now() - report.last.arrived).count();
        position_blend::extrapolate(remote.pos, report.velocity, elapsed, goalPos);
    } else {
        std::memcpy(goalPos, remote.pos, sizeof(goalPos));
    }
    const float drift = position_blend::distance(pos, goalPos);
    const bool forced = Clock::now() < g_forcedUntil;
    const Action action = forced ? Action::Snap : position_blend::classify(drift);
    if (action == Action::Snap) {
        g_forcedUntil = {};
        snap(player, remote, drift);
    } else if (action == Action::Blend) {
        blend(player, pos, quat, goalPos, remote);
    }
}

}  // namespace

namespace state_correction {

void onFrame(const GameFrame& frame) {
    if (frame.type != state_sync::kMsgPlayerState || frame.payload.size() != sizeof(state_sync::PlayerState) ||
        frame.slot != net_pad::peerSlot()) {
        return;
    }
    Sample sample{};
    std::memcpy(&sample.state, frame.payload.data(), sizeof(sample.state));
    sample.arrived = Clock::now();
    std::lock_guard lock(g_mutex);
    g_previous = g_last;
    g_hasPrevious = g_hasLast;
    g_last = sample;
    g_hasLast = true;
    g_freshHp = true;
}

void enable() {
    game_tick::addCallback("state_correction", onTick);
    game_tick::setPostMove(afterMove);
}

void requestForcedCheck() { g_forcedUntil = Clock::now() + kForcedCheckWindow; }

bool latestState(state_sync::PlayerState& out) {
    std::lock_guard lock(g_mutex);
    if (!g_hasLast) return false;
    out = g_last.state;
    return true;
}

}  // namespace state_correction
