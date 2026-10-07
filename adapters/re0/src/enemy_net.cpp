#include "enemy_net.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include "debug_stats.h"
#include "enemy_damage_hook.h"
#include "enemy_hit_wire.h"
#include "enemy_protocol.h"
#include "enemy_registry.h"
#include "game_tick.h"
#include "log.h"
#include "net_pad.h"
#include "player_damage.h"
#include "scene.h"

namespace {

using character_owner::Character;
using debug_stats::Counter;
using enemy_protocol::HitPayload;

constexpr size_t kMaxQueued = 64;
constexpr uint16_t kMaxWireRoom = UINT8_MAX;

struct Pending {
    uint16_t type;
    HitPayload hit;
};

NetClient* g_net = nullptr;
std::mutex g_mutex;
std::vector<Pending> g_queue;  // guarded by g_mutex

int32_t hpOf(uintptr_t enemy) {
    int32_t hp = 0;
    game::readMemory(enemy + game::kEnemyHpOffset, hp);
    return hp;
}

bool positionOf(uintptr_t enemy, float (&out)[3]) { return game::readMemory(enemy + game::kUnitPositionOffset, out); }

game::RandomState readRandom() {
    game::RandomState state{};
    game::readMemory(game::kRandomStateGlobal, state);
    return state;
}

void writeRandom(const game::RandomState& state) { game::writeMemory(game::kRandomStateGlobal, state); }

// The payload of a hit on `enemy`, or false when the enemy has no pool slot or the room id does not fit the wire.
bool encode(uintptr_t enemy, Character attacker, const game::HitPoint& point, const game::HitInfo& info,
            HitPayload& out) {
    const int slot = enemy_registry::slotOf(enemy);
    const uint16_t room = scene::current();
    float enemyPos[3];
    if (slot == enemy_registry::kNoSlot || room > kMaxWireRoom || !positionOf(enemy, enemyPos)) return false;
    out = enemy_hit_wire::encode(static_cast<uint8_t>(slot), static_cast<uint8_t>(attacker),
                                 static_cast<uint8_t>(room), point, enemyPos, info);
    return true;
}

void sendApplied(const HitPayload& hit) {
    if (g_net->send(enemy_protocol::kMsgHitApplied, true, proto::kSlotAll, proto::bytesOf(hit))) {
        debug_stats::count(Counter::HitAppliedSent);
    }
}

// Runs the hit as the owner: its HP and random state are what every replay starts from.
void runAsOwner(uintptr_t enemy, uintptr_t attackerObject, HitPayload& hit, game::HitPoint point,
                game::HitInfo info) {
    const int32_t hpBefore = hpOf(enemy);
    const game::RandomState random = readRandom();
    enemy_hit_wire::stamp(hit, hpBefore, random);
    if (!enemy_damage_hook::runDamage(enemy, attackerObject, point, info)) return;
    sendApplied(hit);
    logger::write("enemy_net: hit slot %u applied hp %d -> %d rng %08x", hit.slot, hpBefore, hpOf(enemy), random[0]);
}

// Runs the owner's hit from the owner's inputs; the local random state is put back afterwards.
void replay(uintptr_t enemy, uintptr_t attackerObject, const HitPayload& hit) {
    const int32_t hpLocal = hpOf(enemy);
    if (hpLocal <= 0 && hit.hpBefore > 0) {
        logger::write("enemy_net: hit dropped (slot %u already dead here)", hit.slot);
        return;
    }
    float enemyPos[3];
    if (!positionOf(enemy, enemyPos)) return;
    game::HitPoint point = enemy_hit_wire::pointOf(hit, enemyPos);
    game::HitInfo info = enemy_hit_wire::infoOf(hit, reinterpret_cast<void*>(attackerObject));
    if (hpLocal != hit.hpBefore) player_damage::setHp(enemy, hit.hpBefore);
    const game::RandomState local = readRandom();
    writeRandom(enemy_hit_wire::randomOf(hit));
    const bool ran = enemy_damage_hook::runDamage(enemy, attackerObject, point, info);
    writeRandom(local);
    if (!ran) return;
    logger::write("enemy_net: hit slot %u replayed hp %d -> %d (was %d here)", hit.slot, hit.hpBefore, hpOf(enemy),
                  hpLocal);
}

// A request is applied as the owner; an applied hit is replayed. A hit queued across a room load is dropped: its
// slot now names an enemy of the new room.
void apply(const Pending& pending) {
    const HitPayload& hit = pending.hit;
    if (hit.attackerCharacterId > static_cast<uint8_t>(Character::Rebecca)) return;
    if (hit.room != scene::current()) {
        logger::write("enemy_net: hit dropped (slot %u in room 0x%x, loaded 0x%x)", hit.slot, hit.room,
                      scene::current());
        return;
    }
    const uintptr_t enemy = enemy_registry::enemyAt(hit.slot);
    const uintptr_t attacker = character_owner::find(static_cast<Character>(hit.attackerCharacterId));
    if (!enemy || !attacker) {
        logger::write("enemy_net: hit dropped (slot %u: %s)", hit.slot, enemy ? "no attacker" : "no enemy");
        return;
    }
    if (pending.type == enemy_protocol::kMsgHitApplied) return replay(enemy, attacker, hit);
    float enemyPos[3];
    if (!positionOf(enemy, enemyPos)) return;
    HitPayload owned = hit;
    runAsOwner(enemy, attacker, owned, enemy_hit_wire::pointOf(hit, enemyPos),
               enemy_hit_wire::infoOf(hit, reinterpret_cast<void*>(attacker)));
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

void applyAsOwner(uintptr_t enemy, Character attacker, uintptr_t attackerObject, const game::HitPoint& point,
                  const game::HitInfo& info) {
    HitPayload hit;
    if (encode(enemy, attacker, point, info, hit)) return runAsOwner(enemy, attackerObject, hit, point, info);
    game::HitPoint localPoint = point;
    game::HitInfo localInfo = info;
    enemy_damage_hook::runDamage(enemy, attackerObject, localPoint, localInfo);
}

bool requestHit(uintptr_t enemy, Character attacker, const game::HitPoint& point, const game::HitInfo& info) {
    HitPayload hit;
    if (!encode(enemy, attacker, point, info, hit)) return false;
    const bool sent = g_net->send(enemy_protocol::kMsgHitRequest, true, static_cast<uint8_t>(net_pad::peerSlot()),
                                  proto::bytesOf(hit));
    if (sent) debug_stats::count(Counter::HitRequestSent);
    return sent;
}

void onFrame(const GameFrame& frame) {
    const bool hitType = frame.type == enemy_protocol::kMsgHitRequest || frame.type == enemy_protocol::kMsgHitApplied;
    if (!hitType || frame.payload.size() != sizeof(HitPayload) || frame.slot != net_pad::peerSlot()) return;
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
