#include "enemy_net.h"

#include <cstring>
#include <mutex>
#include <vector>

#include "debug_stats.h"
#include "enemy_damage_hook.h"
#include "enemy_protocol.h"
#include "enemy_registry.h"
#include "game_tick.h"
#include "net_pad.h"
#include "split_rooms.h"

namespace {

using character_owner::Character;
using debug_stats::Counter;
using enemy_protocol::HitPayload;

constexpr size_t kMaxQueued = 64;

struct Pending {
    uint16_t type;
    HitPayload hit;
};

NetClient* g_net = nullptr;
std::mutex g_mutex;
std::vector<Pending> g_queue;  // guarded by g_mutex

bool send(uint16_t type, uint8_t destSlot, uintptr_t enemy, Character attacker, float distance,
          const game::HitInfo& info) {
    const int slot = enemy_registry::slotOf(enemy);
    if (slot == enemy_registry::kNoSlot) return false;
    const HitPayload hit{static_cast<uint8_t>(slot), static_cast<uint8_t>(attacker), info.flag, 0, distance,
                         info.rangeTier, info.attackType, info.a, info.b};
    const bool sent = g_net->send(type, true, destSlot, proto::bytesOf(hit));
    if (sent) debug_stats::count(type == enemy_protocol::kMsgHitRequest ? Counter::HitRequestSent : Counter::HitAppliedSent);
    return sent;
}

// Rebuilds the hit on this machine and runs it; the host then tells the peer.
void apply(const Pending& pending) {
    const HitPayload& hit = pending.hit;
    if (hit.attackerCharacterId > static_cast<uint8_t>(Character::Rebecca)) return;
    const uintptr_t enemy = enemy_registry::enemyAt(hit.slot);
    const uintptr_t attacker = character_owner::find(static_cast<Character>(hit.attackerCharacterId));
    if (!enemy || !attacker) return;
    game::HitInfo info{hit.rangeTier, hit.attackType, hit.a, hit.b, reinterpret_cast<void*>(attacker), hit.flag, {}};
    if (!enemy_damage_hook::applyNetworkHit(enemy, attacker, hit.distance, info)) return;
    if (pending.type != enemy_protocol::kMsgHitRequest) return;
    if (g_net->send(enemy_protocol::kMsgHitApplied, true, proto::kSlotAll, proto::bytesOf(hit))) {
        debug_stats::count(Counter::HitAppliedSent);
    }
}

void drain() {
    std::vector<Pending> batch;
    {
        std::lock_guard lock(g_mutex);
        batch.swap(g_queue);
    }
    for (const Pending& pending : batch) apply(pending);
}

}  // namespace

namespace enemy_net {

bool requestHit(uintptr_t enemy, Character attacker, float distance, const game::HitInfo& info) {
    return send(enemy_protocol::kMsgHitRequest, static_cast<uint8_t>(net_pad::peerSlot()), enemy, attacker, distance,
                info);
}

void announceHit(uintptr_t enemy, Character attacker, float distance, const game::HitInfo& info) {
    send(enemy_protocol::kMsgHitApplied, proto::kSlotAll, enemy, attacker, distance, info);
}

void onFrame(const GameFrame& frame) {
    if (split_rooms::apart()) return;  // the peer's enemies are in another room
    const bool host = character_owner::isHost();
    const bool expected = frame.type == (host ? enemy_protocol::kMsgHitRequest : enemy_protocol::kMsgHitApplied);
    if (!expected || frame.payload.size() != sizeof(HitPayload) || frame.slot != net_pad::peerSlot()) return;
    debug_stats::count(frame.type == enemy_protocol::kMsgHitRequest ? Counter::HitRequestReceived
                                                                    : Counter::HitAppliedReceived);
    Pending pending{frame.type, {}};
    std::memcpy(&pending.hit, frame.payload.data(), sizeof(pending.hit));
    std::lock_guard lock(g_mutex);
    if (g_queue.size() < kMaxQueued) g_queue.push_back(pending);
}

void enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("enemy_net", drain);
}

}  // namespace enemy_net
