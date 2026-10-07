#include "enemy_state.h"

#include <array>
#include <chrono>
#include <cstring>
#include <mutex>
#include <vector>

#include "debug_stats.h"
#include "enemy_action.h"
#include "enemy_follow_rule.h"
#include "enemy_protocol.h"
#include "enemy_registry.h"
#include "enemy_target.h"
#include "game.h"
#include "game_tick.h"
#include "log.h"
#include "net_pad.h"
#include "player_damage.h"
#include "scene.h"
#include "split_rooms.h"

namespace {

using Clock = std::chrono::steady_clock;
using enemy_protocol::EnemyEntry;

constexpr auto kSendInterval = std::chrono::milliseconds(50);
constexpr int64_t kLogIntervalMs = 1000;
constexpr uint16_t kMaxWireRoom = UINT8_MAX;

NetClient* g_net = nullptr;
Clock::time_point g_lastSend;

struct Snapshot {
    uint8_t count = 0;
    uint8_t room = 0;
    std::array<EnemyEntry, game::kEnemyPoolSlots> entries{};
};

std::mutex g_mutex;
Snapshot g_latest;     // guarded by g_mutex
bool g_fresh = false;  // guarded by g_mutex

// What this machine knows about one enemy the peer owns (game thread only).
struct Follow {
    bool valid = false;
    uint8_t target = enemy_protocol::kNoTarget;
    int32_t ownerHp = 0;
    enemy_follow_rule::Correction correction;
    enemy_follow_rule::HpBackstop hp;
    enemy_action_rule::Cue cue;
};

std::array<Follow, game::kEnemyPoolSlots> g_follow{};
std::array<bool, game::kEnemyPoolSlots> g_mismatchLogged{};
uint16_t g_followRoom = scene::kNone;  // the scene g_follow describes

// One log line per kind and second.
struct LogLimit {
    int64_t lastMs = enemy_follow_rule::kNever;
    bool due(int64_t nowMs) {
        if (nowMs - lastMs < kLogIntervalMs) return false;
        lastMs = nowMs;
        return true;
    }
};
LogLimit g_correctLog;
LogLimit g_snapLog;
LogLimit g_cueLog;
LogLimit g_hpLog;

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

bool readPosition(uintptr_t enemy, float (&out)[3]) { return game::readMemory(enemy + game::kUnitPositionOffset, out); }

void sendState() {
    const uint16_t room = scene::current();
    if (room > kMaxWireRoom) return;
    std::vector<uint8_t> payload(enemy_protocol::kStateHeaderSize);
    uint8_t count = 0;
    for (int slot = 0; slot < game::kEnemyPoolSlots; ++slot) {
        const uintptr_t enemy = enemy_registry::enemyAt(slot);
        EnemyEntry entry{};
        entry.slot = static_cast<uint8_t>(slot);
        entry.vtable = static_cast<uint32_t>(game::readPointer(enemy));
        entry.target = enemy_target::read(enemy);
        enemy_action_rule::Action action;
        if (!enemy_registry::isEnemy(enemy) || !game::readMemory(enemy + game::kEnemyHpOffset, entry.hp) ||
            !game::readTransform(enemy, entry.pos, entry.quat) || !enemy_action::read(enemy, action)) {
            continue;
        }
        std::memcpy(entry.action, action.word, sizeof(entry.action));
        const auto* bytes = reinterpret_cast<const uint8_t*>(&entry);
        payload.insert(payload.end(), bytes, bytes + sizeof(entry));
        ++count;
    }
    payload[enemy_protocol::kStateCountByte] = count;
    payload[enemy_protocol::kStateRoomByte] = static_cast<uint8_t>(room);
    if (g_net->send(enemy_protocol::kMsgEnemyState, false, proto::kSlotAll, payload)) {
        debug_stats::count(debug_stats::Counter::EnemyStateSent);
    }
}

// This machine runs enemies the peer owns: it has a peer, is in the same room, and the peer runs the room.
bool following() { return net_pad::active() && !split_rooms::apart() && !split_rooms::localEnemyAuthority(); }

void resetFollow() {
    g_follow.fill(Follow{});
    g_mismatchLogged.fill(false);
    g_followRoom = scene::current();
}

void logMismatchOnce(const EnemyEntry& entry, const char* reason) {
    if (g_mismatchLogged[entry.slot]) return;
    g_mismatchLogged[entry.slot] = true;
    debug_stats::count(debug_stats::Counter::EnemyMismatches);
    logger::write("enemy_state: slot %u %s (owner vtable 0x%x)", entry.slot, reason, entry.vtable);
}

void applyHp(Follow& follow, const EnemyEntry& entry, uintptr_t enemy, int64_t now) {
    int32_t hp = 0;
    if (!game::readMemory(enemy + game::kEnemyHpOffset, hp) || !follow.hp.applies(hp, entry.hp, now)) return;
    player_damage::setHp(enemy, entry.hp);
    if (g_hpLog.due(now)) logger::write("enemy_state: slot %u hp %d -> %d (owner, no hit event)", entry.slot, hp, entry.hp);
}

// Measures the error to the owner's enemy once; a jump is snapped, pose and all.
void measure(Follow& follow, const EnemyEntry& entry, uintptr_t enemy, int64_t now) {
    float pos[3];
    if (!readPosition(enemy, pos)) return;
    const float drift = position_blend::distance(pos, entry.pos);
    switch (enemy_follow_rule::start(follow.correction, pos, entry.pos)) {
        case enemy_follow_rule::Start::None:
            return;
        case enemy_follow_rule::Start::Correct:
            if (g_correctLog.due(now)) logger::write("enemy_state: slot %u corrected drift %.1f", entry.slot, drift);
            return;
        case enemy_follow_rule::Start::Snap:
            game::writeTransform(enemy, entry.pos, entry.quat);
            debug_stats::count(debug_stats::Counter::Snaps);
            if (g_snapLog.due(now)) logger::write("enemy_state: snapped slot %u drift %.1f", entry.slot, drift);
            return;
    }
}

void applyEntry(const EnemyEntry& entry, int64_t now) {
    if (entry.slot >= game::kEnemyPoolSlots) return;
    Follow& follow = g_follow[entry.slot];
    const uintptr_t enemy = enemy_registry::enemyAt(entry.slot);
    if (!enemy || game::readPointer(enemy) != entry.vtable) {
        follow = Follow{};
        return logMismatchOnce(entry, enemy ? "spawned a different class" : "has no local enemy");
    }
    follow.valid = true;
    follow.target = entry.target;
    follow.ownerHp = entry.hp;
    applyHp(follow, entry, enemy, now);
    if (entry.hp > 0) measure(follow, entry, enemy, now);
    enemy_action_rule::Action owner;
    std::memcpy(owner.word, entry.action, sizeof(owner.word));
    follow.cue.observeOwner(owner, now);
}

void takeLatest(int64_t now) {
    Snapshot snapshot;
    {
        std::lock_guard lock(g_mutex);
        if (!g_fresh) return;
        snapshot = g_latest;
        g_fresh = false;
    }
    if (snapshot.room != scene::current()) return;  // the owner's previous room: its slots name other enemies
    for (uint8_t i = 0; i < snapshot.count; ++i) applyEntry(snapshot.entries[i], now);
}

// The enemy keeps its own movement; a share of the measured error is added on top.
void correct(Follow& follow, uintptr_t enemy) {
    float delta[3];
    enemy_follow_rule::step(follow.correction, delta);
    float pos[3];
    if ((delta[0] == 0.0f && delta[1] == 0.0f && delta[2] == 0.0f) || !readPosition(enemy, pos)) return;
    for (int i = 0; i < 3; ++i) pos[i] += delta[i];
    game::writeMemory(enemy + game::kUnitPositionOffset, pos);
}

// A new owner decision the local AI did not reach by itself starts through the class's own setAction.
void cue(Follow& follow, uintptr_t enemy, int slot, int64_t now) {
    enemy_action_rule::Action local;
    if (!enemy_action::read(enemy, local) || !follow.cue.due(local, now)) return;
    const enemy_action_rule::Action& owner = follow.cue.owner();
    if (!enemy_action::request(enemy, owner)) return;
    debug_stats::count(debug_stats::Counter::EnemyActionRequests);
    if (g_cueLog.due(now)) {
        logger::write("enemy_state: slot %d cued action (%d,%d,%d,%d), local (%d,%d,%d,%d)", slot, owner.word[0],
                      owner.word[1], owner.word[2], owner.word[3], local.word[0], local.word[1], local.word[2],
                      local.word[3]);
    }
}

void followOwner() {
    const int64_t now = nowMs();
    takeLatest(now);
    for (int slot = 0; slot < game::kEnemyPoolSlots; ++slot) {
        Follow& state = g_follow[slot];
        const uintptr_t enemy = enemy_registry::enemyAt(slot);
        if (!state.valid || !enemy || state.ownerHp <= 0) continue;
        correct(state, enemy);
        cue(state, enemy, slot, now);
    }
}

void onTick() {
    if (scene::current() != g_followRoom || !following()) resetFollow();
    if (following()) return followOwner();
    if (!net_pad::active() || split_rooms::apart()) return;  // apart, each machine runs its own room's enemies
    const auto now = Clock::now();
    if (now - g_lastSend < kSendInterval) return;
    g_lastSend = now;
    sendState();
}

}  // namespace

namespace enemy_state {

uint8_t ownerTarget(uintptr_t enemy) {
    if (!following()) return enemy_protocol::kNoTarget;
    const int slot = enemy_registry::slotOf(enemy);
    if (slot == enemy_registry::kNoSlot || !g_follow[slot].valid || g_follow[slot].ownerHp <= 0) {
        return enemy_protocol::kNoTarget;
    }
    return g_follow[slot].target;
}

void onHitReplayed(uint8_t slot) {
    if (slot < game::kEnemyPoolSlots) g_follow[slot].hp.onHitReplayed(nowMs());
}

void onFrame(const GameFrame& frame) {
    if (frame.type != enemy_protocol::kMsgEnemyState || frame.slot != net_pad::peerSlot() ||
        frame.payload.size() < enemy_protocol::kStateHeaderSize) {
        return;
    }
    const uint8_t count = frame.payload[enemy_protocol::kStateCountByte];
    if (count > game::kEnemyPoolSlots ||
        frame.payload.size() != enemy_protocol::kStateHeaderSize + count * sizeof(EnemyEntry)) {
        return;
    }
    debug_stats::count(debug_stats::Counter::EnemyStateReceived);
    std::lock_guard lock(g_mutex);
    g_latest.count = count;
    g_latest.room = frame.payload[enemy_protocol::kStateRoomByte];
    std::memcpy(g_latest.entries.data(), frame.payload.data() + enemy_protocol::kStateHeaderSize,
                count * sizeof(EnemyEntry));
    g_fresh = true;
}

void enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("enemy_state", onTick);
}

}  // namespace enemy_state
