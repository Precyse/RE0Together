#include "enemy_state.h"

#include <array>
#include <chrono>
#include <cstring>
#include <mutex>
#include <vector>

#include "debug_stats.h"
#include "enemy_action.h"
#include "enemy_protocol.h"
#include "enemy_puppet_rule.h"
#include "enemy_registry.h"
#include "game.h"
#include "game_tick.h"
#include "log.h"
#include "net_pad.h"
#include "player_damage.h"
#include "position_blend.h"
#include "split_rooms.h"

namespace {

using Clock = std::chrono::steady_clock;
using enemy_protocol::EnemyEntry;

constexpr auto kSendInterval = std::chrono::milliseconds(50);
constexpr auto kLogInterval = std::chrono::seconds(1);

NetClient* g_net = nullptr;
Clock::time_point g_lastSend;
Clock::time_point g_lastLog;

struct Snapshot {
    uint8_t count = 0;
    std::array<EnemyEntry, game::kEnemyPoolSlots> entries{};
};

std::mutex g_mutex;
Snapshot g_latest;     // guarded by g_mutex
bool g_fresh = false;  // guarded by g_mutex
std::array<bool, game::kEnemyPoolSlots> g_mismatchLogged{};
std::array<enemy_puppet_rule::Track, game::kEnemyPoolSlots> g_tracks{};  // game thread only: what the owner last said
std::array<enemy_action_rule::Sync, game::kEnemyPoolSlots> g_actions{};  // game thread only

bool logDue() {
    const auto now = Clock::now();
    if (now - g_lastLog < kLogInterval) return false;
    g_lastLog = now;
    return true;
}

// The enemy's behaviour record into a snapshot entry.
bool readAction(uintptr_t enemy, int32_t (&out)[4]) {
    enemy_action_rule::Action action;
    if (!enemy_action::read(enemy, action)) return false;
    std::memcpy(out, action.word, sizeof(action.word));
    return true;
}

void sendState() {
    std::vector<uint8_t> payload(enemy_protocol::kStateHeaderSize);
    uint8_t count = 0;
    for (int slot = 0; slot < game::kEnemyPoolSlots; ++slot) {
        const uintptr_t enemy = enemy_registry::enemyAt(slot);
        EnemyEntry entry{};
        entry.slot = static_cast<uint8_t>(slot);
        entry.vtable = static_cast<uint32_t>(game::readPointer(enemy));
        if (!enemy_registry::isEnemy(enemy) || !game::readMemory(enemy + game::kEnemyHpOffset, entry.hp) ||
            !game::readTransform(enemy, entry.pos, entry.quat) || !readAction(enemy, entry.action)) {
            continue;
        }
        const auto* bytes = reinterpret_cast<const uint8_t*>(&entry);
        payload.insert(payload.end(), bytes, bytes + sizeof(entry));
        ++count;
    }
    payload[0] = count;
    if (g_net->send(enemy_protocol::kMsgEnemyState, false, proto::kSlotAll, payload)) {
        debug_stats::count(debug_stats::Counter::EnemyStateSent);
    }
}

void logOnce(const EnemyEntry& entry, const char* reason) {
    if (g_mismatchLogged[entry.slot]) return;
    g_mismatchLogged[entry.slot] = true;
    debug_stats::count(debug_stats::Counter::EnemyMismatches);
    logger::write("enemy_state: slot %u %s (host vtable 0x%x)", entry.slot, reason, entry.vtable);
}

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

// This machine shows enemies owned by the peer: it has a peer, is not apart from it, and the peer runs the room.
bool puppetActive() {
    return net_pad::active() && !split_rooms::apart() && !split_rooms::localEnemyAuthority();
}

void applyHp(const EnemyEntry& entry, uintptr_t enemy) {
    int32_t hp = 0;
    if (!game::readMemory(enemy + game::kEnemyHpOffset, hp) || hp == entry.hp) return;
    player_damage::setHp(enemy, entry.hp);
    if (logDue()) logger::write("enemy_state: slot %u hp %d -> %d", entry.slot, hp, entry.hp);
}

void applyEntry(const EnemyEntry& entry) {
    if (entry.slot >= game::kEnemyPoolSlots) return;
    enemy_puppet_rule::Track& track = g_tracks[entry.slot];
    const uintptr_t enemy = enemy_registry::enemyAt(entry.slot);
    if (!enemy || game::readPointer(enemy) != entry.vtable) {
        track.valid = false;
        g_actions[entry.slot].reset();
        return logOnce(entry, enemy ? "spawned a different class" : "has no local enemy");
    }
    applyHp(entry, enemy);
    enemy_puppet_rule::observe(track, entry.pos, entry.quat, entry.hp, nowMs());
    enemy_action_rule::Action owner;
    std::memcpy(owner.word, entry.action, sizeof(owner.word));
    g_actions[entry.slot].observeOwner(owner, nowMs());
}

// Moves a puppet toward where the owner's enemy is now: blended while it lags, snapped only after a jump.
void followPose(uintptr_t enemy, int slot, const enemy_puppet_rule::Track& track, int64_t now) {
    float pos[3];
    float quat[4];
    if (!game::readTransform(enemy, pos, quat)) return;
    float aim[3];
    enemy_puppet_rule::aim(track, now, aim);
    const float drift = position_blend::distance(pos, aim);
    const enemy_puppet_rule::Step step = enemy_puppet_rule::stepFor(drift);
    if (step == enemy_puppet_rule::Step::Hold) return;
    if (step == enemy_puppet_rule::Step::Snap) {
        game::writeTransform(enemy, aim, track.quat);
        debug_stats::count(debug_stats::Counter::Snaps);
        if (logDue()) logger::write("enemy_state: snapped slot %d, drift=%.1f", slot, drift);
        return;
    }
    float blendedPos[3];
    float blendedQuat[4];
    position_blend::blendPosition(pos, aim, blendedPos);
    position_blend::blendRotation(quat, track.quat, blendedQuat);
    game::writeTransform(enemy, blendedPos, blendedQuat);
}

// A puppet that keeps doing something else than the owner is told the owner's behaviour through the class's own
// setAction, so its handlers start the matching animation (enemy_action_rule.h).
void matchAction(uintptr_t enemy, int slot, int64_t now) {
    enemy_action_rule::Action local;
    if (!enemy_action::read(enemy, local) || !g_actions[slot].due(local, now)) return;
    if (enemy_action::request(enemy, g_actions[slot].owner())) debug_stats::count(debug_stats::Counter::EnemyActionRequests);

}

// The pose of a living puppet follows the owner's.
void followTrack(int slot, const enemy_puppet_rule::Track& track, int64_t now) {
    const uintptr_t enemy = enemy_registry::enemyAt(slot);
    if (!enemy || track.hp <= 0) return;
    followPose(enemy, slot, track, now);
    matchAction(enemy, slot, now);
}

void takeLatest() {
    Snapshot snapshot;
    {
        std::lock_guard lock(g_mutex);
        if (!g_fresh) return;
        snapshot = g_latest;
        g_fresh = false;
    }
    for (uint8_t i = 0; i < snapshot.count; ++i) applyEntry(snapshot.entries[i]);
}

void driveTracks() {
    takeLatest();
    const int64_t now = nowMs();
    for (int slot = 0; slot < game::kEnemyPoolSlots; ++slot) {
        if (g_tracks[slot].valid) followTrack(slot, g_tracks[slot], now);
    }
}

void onTick() {
    if (puppetActive()) return driveTracks();
    for (auto& track : g_tracks) track.valid = false;
    for (auto& sync : g_actions) sync.reset();
    if (!net_pad::active() || split_rooms::apart()) return;  // apart, each machine runs its own room's enemies
    const auto now = Clock::now();
    if (now - g_lastSend < kSendInterval) return;
    g_lastSend = now;
    sendState();
}

}  // namespace

namespace enemy_state {

void onFrame(const GameFrame& frame) {
    if (frame.type != enemy_protocol::kMsgEnemyState || split_rooms::localEnemyAuthority() ||
        frame.slot != net_pad::peerSlot() || frame.payload.size() < enemy_protocol::kStateHeaderSize) {
        return;
    }
    const uint8_t count = frame.payload[0];
    if (count > game::kEnemyPoolSlots ||
        frame.payload.size() != enemy_protocol::kStateHeaderSize + count * sizeof(EnemyEntry)) {
        return;
    }
    debug_stats::count(debug_stats::Counter::EnemyStateReceived);
    std::lock_guard lock(g_mutex);
    g_latest.count = count;
    std::memcpy(g_latest.entries.data(), frame.payload.data() + enemy_protocol::kStateHeaderSize,
                count * sizeof(EnemyEntry));
    g_fresh = true;
}


void enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("enemy_state", onTick);
}

}  // namespace enemy_state
