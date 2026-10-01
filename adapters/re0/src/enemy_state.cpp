#include "enemy_state.h"

#include <array>
#include <chrono>
#include <cstring>
#include <mutex>
#include <vector>

#include "debug_stats.h"
#include "enemy_protocol.h"
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

constexpr float kEnemySnapDistance = 80.0f;
constexpr auto kSendInterval = std::chrono::milliseconds(100);
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

bool logDue() {
    const auto now = Clock::now();
    if (now - g_lastLog < kLogInterval) return false;
    g_lastLog = now;
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
            !game::readTransform(enemy, entry.pos, entry.quat)) {
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

void applyEntry(const EnemyEntry& entry) {
    if (entry.slot >= game::kEnemyPoolSlots) return;
    const uintptr_t enemy = enemy_registry::enemyAt(entry.slot);
    if (!enemy) return logOnce(entry, "has no local enemy");
    if (game::readPointer(enemy) != entry.vtable) return logOnce(entry, "spawned a different class");

    int32_t hp = 0;
    if (game::readMemory(enemy + game::kEnemyHpOffset, hp) && hp != entry.hp) {
        player_damage::setHp(enemy, entry.hp);
        if (logDue()) logger::write("enemy_state: slot %u hp %d -> %d", entry.slot, hp, entry.hp);
    }
    float pos[3];
    float quat[4];
    if (!game::readTransform(enemy, pos, quat)) return;
    const float drift = position_blend::distance(pos, entry.pos);
    if (drift <= kEnemySnapDistance) return;
    game::writeTransform(enemy, entry.pos, entry.quat);
    debug_stats::count(debug_stats::Counter::Snaps);
    if (logDue()) logger::write("enemy_state: snapped slot %u, drift=%.1f", entry.slot, drift);
}

void applyLatest() {
    Snapshot snapshot;
    {
        std::lock_guard lock(g_mutex);
        if (!g_fresh) return;
        snapshot = g_latest;
        g_fresh = false;
    }
    for (uint8_t i = 0; i < snapshot.count; ++i) applyEntry(snapshot.entries[i]);
}

void onTick() {
    if (!net_pad::active() || split_rooms::apart()) return;  // apart, each machine runs its own room's enemies
    if (!split_rooms::localEnemyAuthority()) return applyLatest();
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
