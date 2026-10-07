#include "enemy_state.h"

#include <array>
#include <chrono>
#include <cstring>
#include <mutex>
#include <vector>

#include "debug_stats.h"
#include "enemy_follow_rule.h"
#include "enemy_protocol.h"
#include "enemy_registry.h"
#include "enemy_target.h"
#include "enemy_decision.h"
#include "game.h"
#include "game_tick.h"
#include "log.h"
#include "net_pad.h"
#include "player_damage.h"
#include "position_blend.h"
#include "scene.h"
#include "split_rooms.h"

namespace {

using Clock = std::chrono::steady_clock;
using enemy_protocol::EnemyEntry;
using enemy_protocol::StateHeader;

constexpr auto kSendInterval = std::chrono::milliseconds(50);
constexpr auto kLogInterval = std::chrono::seconds(1);
constexpr uint16_t kMaxWireRoom = UINT8_MAX;

NetClient* g_net = nullptr;
Clock::time_point g_lastSend;
uint16_t g_sendSeq = 0;  // game thread only

struct Snapshot {
    StateHeader header{};
    std::array<EnemyEntry, game::kEnemyPoolSlots> entries{};
};

std::mutex g_mutex;
Snapshot g_latest;          // guarded by g_mutex
bool g_fresh = false;       // guarded by g_mutex
bool g_hasSeq = false;      // guarded by g_mutex
uint16_t g_lastSeq = 0;     // guarded by g_mutex

// What this machine knows about one enemy the peer owns (game thread only).
struct Follow {
    bool valid = false;
    bool aligned = false;  // the one-time alignment of following start is done
    uint8_t target = enemy_protocol::kNoTarget;
    int32_t ownerHp = 0;
};

std::array<Follow, game::kEnemyPoolSlots> g_follow{};
std::array<bool, game::kEnemyPoolSlots> g_mismatchLogged{};
uint16_t g_followRoom = scene::kNone;  // the scene g_follow describes
int g_ticksSinceOwner = 0;  // follower ticks since following started or the last owner snapshot
Clock::time_point g_lastDriftLog;

bool readPosition(uintptr_t enemy, float (&out)[3]) { return game::readMemory(enemy + game::kUnitPositionOffset, out); }

int32_t readHp(uintptr_t enemy) {
    int32_t hp = 0;
    game::readMemory(enemy + game::kEnemyHpOffset, hp);
    return hp;
}

void sendState() {
    const uint16_t room = scene::current();
    if (room > kMaxWireRoom) return;
    std::vector<uint8_t> payload(sizeof(StateHeader));
    uint8_t count = 0;
    for (int slot = 0; slot < game::kEnemyPoolSlots; ++slot) {
        const uintptr_t enemy = enemy_registry::enemyAt(slot);
        EnemyEntry entry{};
        entry.slot = static_cast<uint8_t>(slot);
        entry.vtable = static_cast<uint32_t>(game::readPointer(enemy));
        entry.target = enemy_target::read(enemy);
        if (!enemy_registry::isEnemy(enemy) || !game::readMemory(enemy + game::kEnemyHpOffset, entry.hp) ||
            !game::readTransform(enemy, entry.pos, entry.quat) ||
            !game::readMemory(enemy + game::kEnemyActionOffset, entry.action)) {
            continue;
        }
        const auto* bytes = reinterpret_cast<const uint8_t*>(&entry);
        payload.insert(payload.end(), bytes, bytes + sizeof(entry));
        ++count;
    }
    const StateHeader header{count, static_cast<uint8_t>(room), g_sendSeq++};
    std::memcpy(payload.data(), &header, sizeof(header));
    if (g_net->send(enemy_protocol::kMsgEnemyState, false, proto::kSlotAll, payload)) {
        debug_stats::count(debug_stats::Counter::EnemyStateSent);
    }
}

// Shares the loaded room with the peer, and the peer runs it.
bool following() { return net_pad::active() && !split_rooms::apart() && !split_rooms::localEnemyAuthority(); }

// Also forgets the owner's snapshot numbering: the next owner (or a restarted one) counts from its own start.
void resetFollow() {
    {
        std::lock_guard lock(g_mutex);
        g_hasSeq = false;
        g_fresh = false;
    }
    g_follow.fill(Follow{});
    enemy_decision::reset();
    g_mismatchLogged.fill(false);
    g_followRoom = scene::current();
    g_ticksSinceOwner = 0;
}

void logMismatchOnce(const EnemyEntry& entry, const char* reason) {
    if (g_mismatchLogged[entry.slot]) return;
    g_mismatchLogged[entry.slot] = true;
    debug_stats::count(debug_stats::Counter::EnemyMismatches);
    logger::write("enemy_state: slot %u %s (owner vtable 0x%x)", entry.slot, reason, entry.vtable);
}

// Following starts mid-room (this machine walked into a room the peer already runs, or became the follower): the
// enemy takes the owner's pose and HP once, and its think step the owner's current record until the first decision.
void align(const EnemyEntry& entry, uintptr_t enemy) {
    float pos[3];
    const float drift = readPosition(enemy, pos) ? position_blend::distance(pos, entry.pos) : 0.0f;
    if (enemy_follow_rule::realigns(drift)) game::writeTransform(enemy, entry.pos, entry.quat);
    const int32_t hp = readHp(enemy);
    if (hp > 0 && hp != entry.hp) player_damage::setHp(enemy, entry.hp);
    enemy_follow_rule::Decision current;
    current.vtable = entry.vtable;
    std::memcpy(current.action.word, entry.action, sizeof(entry.action));
    std::memcpy(current.pos, entry.pos, sizeof(entry.pos));
    std::memcpy(current.quat, entry.quat, sizeof(entry.quat));
    if (enemy_follow_rule::seedable(current.action)) enemy_decision::seed(entry.slot, current);
    logger::write("enemy_state: slot %u aligned with the owner (drift %.1f, hp %d -> %d)", entry.slot, drift, hp,
                  entry.hp);
}

void logDrift(const EnemyEntry& entry, uintptr_t enemy) {
    float pos[3];
    if (!readPosition(enemy, pos)) return;
    const float drift = position_blend::distance(pos, entry.pos);
    const auto now = Clock::now();
    if (!enemy_follow_rule::realigns(drift) || now - g_lastDriftLog < kLogInterval) return;
    g_lastDriftLog = now;
    logger::write("enemy_state: slot %u drift %.1f (record %d,%d)", entry.slot, drift, entry.action[0], entry.action[1]);
}

void applyEntry(const EnemyEntry& entry) {
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
    if (!follow.aligned) {
        follow.aligned = true;
        return align(entry, enemy);
    }
    logDrift(entry, enemy);
}

void takeLatest() {
    Snapshot snapshot;
    {
        std::lock_guard lock(g_mutex);
        if (!g_fresh) return;
        snapshot = g_latest;
        g_fresh = false;
    }
    if (snapshot.header.room != scene::current()) return;  // the owner's previous room: its slots name other enemies
    g_ticksSinceOwner = 0;
    for (uint8_t i = 0; i < snapshot.header.count; ++i) applyEntry(snapshot.entries[i]);
}

void onTick() {
    if (scene::current() != g_followRoom || !following()) resetFollow();
    if (following()) {
        ++g_ticksSinceOwner;
        return takeLatest();
    }
    if (!enemy_state::leadsPeer()) return;
    const auto now = Clock::now();
    if (now - g_lastSend < kSendInterval) return;
    g_lastSend = now;
    sendState();
}

}  // namespace

namespace enemy_state {

bool followsOwner() { return enemy_follow_rule::thinksForOwner(following(), g_ticksSinceOwner); }

bool leadsPeer() { return net_pad::active() && !split_rooms::apart() && split_rooms::localEnemyAuthority(); }

uint8_t ownerTarget(uintptr_t enemy) {
    if (!followsOwner()) return enemy_protocol::kNoTarget;
    const int slot = enemy_registry::slotOf(enemy);
    if (slot == enemy_registry::kNoSlot || !g_follow[slot].valid || g_follow[slot].ownerHp <= 0) {
        return enemy_protocol::kNoTarget;
    }
    return g_follow[slot].target;
}

void forgetSlot(uint8_t slot) {
    if (slot >= game::kEnemyPoolSlots) return;
    g_follow[slot] = Follow{};
    g_mismatchLogged[slot] = false;
}

void onFrame(const GameFrame& frame) {
    if (frame.type != enemy_protocol::kMsgEnemyState || frame.slot != net_pad::peerSlot() ||
        frame.payload.size() < sizeof(StateHeader)) {
        return;
    }
    StateHeader header;
    std::memcpy(&header, frame.payload.data(), sizeof(header));
    if (header.count > game::kEnemyPoolSlots ||
        frame.payload.size() != sizeof(StateHeader) + header.count * sizeof(EnemyEntry)) {
        return;
    }
    std::lock_guard lock(g_mutex);
    if (g_hasSeq && !enemy_follow_rule::newer(header.seq, g_lastSeq)) return;
    g_hasSeq = true;
    g_lastSeq = header.seq;
    debug_stats::count(debug_stats::Counter::EnemyStateReceived);
    g_latest.header = header;
    std::memcpy(g_latest.entries.data(), frame.payload.data() + sizeof(StateHeader), header.count * sizeof(EnemyEntry));
    g_fresh = true;
}

void enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("enemy_state", onTick);
}

}  // namespace enemy_state
