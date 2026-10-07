#include "enemy_net.h"

#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <variant>

#include "debug_stats.h"
#include "enemy_damage_hook.h"
#include "enemy_hit_wire.h"
#include "enemy_protocol.h"
#include "enemy_registry.h"
#include "enemy_think.h"
#include "game_tick.h"
#include "log.h"
#include "net_pad.h"
#include "player_damage.h"
#include "scene.h"

namespace {

using character_owner::Character;
using debug_stats::Counter;
using enemy_protocol::HitPayload;

// While the game does not tick (a door, a held world) events wait here; the oldest go first, they are the stale ones.
constexpr size_t kMaxQueued = 256;
constexpr uint16_t kMaxWireRoom = UINT8_MAX;

struct Hit {
    uint16_t type;  // HIT_REQUEST or HIT_APPLIED
    HitPayload payload;
};
using Event = std::variant<Hit, enemy_protocol::Decision>;

NetClient* g_net = nullptr;
std::mutex g_mutex;
std::deque<Event> g_queue;  // guarded by g_mutex
uint16_t g_decisionSeq = 0;  // game thread only

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

// The loaded room as a wire byte, or false when it has no id that fits.
bool wireRoom(uint8_t& out) {
    const uint16_t room = scene::current();
    if (room > kMaxWireRoom) return false;
    out = static_cast<uint8_t>(room);
    return true;
}

// The payload of a hit on `enemy`, or false when the enemy has no pool slot or the room id does not fit the wire.
bool encode(uintptr_t enemy, uint8_t attackerId, const game::HitPoint& point, const game::HitInfo& info,
            HitPayload& out) {
    const int slot = enemy_registry::slotOf(enemy);
    uint8_t room = 0;
    float enemyPos[3];
    if (slot == enemy_registry::kNoSlot || !wireRoom(room) || !positionOf(enemy, enemyPos)) return false;
    out = enemy_hit_wire::encode(static_cast<uint8_t>(slot), attackerId, room, point, enemyPos, info);
    return true;
}

void sendApplied(const HitPayload& hit) {
    if (g_net->send(enemy_protocol::kMsgHitApplied, true, proto::kSlotAll, proto::bytesOf(hit))) {
        debug_stats::count(Counter::HitAppliedSent);
    }
}

// Runs the hit as the owner: its HP and random state are what every replay starts from, its HP after is the outcome.
void runAsOwner(uintptr_t enemy, uintptr_t attackerObject, HitPayload& hit, game::HitPoint point,
                game::HitInfo info) {
    const game::RandomState random = readRandom();
    enemy_hit_wire::stamp(hit, hpOf(enemy), random);
    if (!enemy_damage_hook::runDamage(enemy, attackerObject, point, info)) return;
    hit.hpAfter = hpOf(enemy);
    sendApplied(hit);
    logger::write("enemy_net: hit slot %u applied hp %d -> %d rng %08x%s", hit.slot, hit.hpBefore, hit.hpAfter,
                  random[0], hit.attackerCharacterId == enemy_protocol::kNoAttacker ? " (not a player)" : "");
}

// Damage no player dealt: there is no attacker to rebuild on the peer, so only its HP outcome travels.
void applyOutcome(uintptr_t enemy, const HitPayload& hit) {
    const int32_t hpLocal = hpOf(enemy);
    if (hpLocal <= 0 || hpLocal == hit.hpAfter) return;
    player_damage::setHp(enemy, hit.hpAfter);
    logger::write("enemy_net: slot %u hp %d -> %d (owner's damage not by a player)", hit.slot, hpLocal, hit.hpAfter);
}

// Runs the owner's hit from the owner's inputs; the local random state is put back afterwards. The owner's outcome is
// the result: a replay that ended elsewhere (a branch that read local state) is set to it and logged.
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
    enemy_think::onHitReplayed(hit.slot);
    const int32_t hpReplayed = hpOf(enemy);
    if (hpReplayed != hit.hpAfter) {
        player_damage::setHp(enemy, hit.hpAfter);
        logger::write("enemy_net: hit slot %u replay diverged: hp %d, owner %d", hit.slot, hpReplayed, hit.hpAfter);
    }
    logger::write("enemy_net: hit slot %u replayed hp %d -> %d (was %d here)", hit.slot, hit.hpBefore, hit.hpAfter,
                  hpLocal);
}

bool validAttacker(const Hit& hit) {
    const uint8_t id = hit.payload.attackerCharacterId;
    if (id <= static_cast<uint8_t>(Character::Rebecca)) return true;
    return hit.type == enemy_protocol::kMsgHitApplied && id == enemy_protocol::kNoAttacker;
}

// A request is applied as the owner; an applied hit is replayed. A hit queued across a room load is dropped: its
// slot now names an enemy of the new room.
void applyHit(const Hit& event) {
    const HitPayload& hit = event.payload;
    if (!validAttacker(event)) return;
    if (hit.room != scene::current()) {
        logger::write("enemy_net: hit dropped (slot %u in room 0x%x, loaded 0x%x)", hit.slot, hit.room,
                      scene::current());
        return;
    }
    const uintptr_t enemy = enemy_registry::enemyAt(hit.slot);
    if (!enemy) {
        logger::write("enemy_net: hit dropped (slot %u: no enemy)", hit.slot);
        return;
    }
    if (hit.attackerCharacterId == enemy_protocol::kNoAttacker) return applyOutcome(enemy, hit);
    const uintptr_t attacker = character_owner::find(static_cast<Character>(hit.attackerCharacterId));
    if (!attacker) {
        logger::write("enemy_net: hit dropped (slot %u: no attacker)", hit.slot);
        return;
    }
    if (event.type == enemy_protocol::kMsgHitApplied) return replay(enemy, attacker, hit);
    float enemyPos[3];
    if (!positionOf(enemy, enemyPos)) return;
    HitPayload owned = hit;
    runAsOwner(enemy, attacker, owned, enemy_hit_wire::pointOf(hit, enemyPos),
               enemy_hit_wire::infoOf(hit, reinterpret_cast<void*>(attacker)));
}

void applyDecision(const enemy_protocol::Decision& wire) {
    if (wire.room != scene::current() || wire.slot >= game::kEnemyPoolSlots) return;
    enemy_follow_rule::Decision decision;
    std::memcpy(decision.action.word, wire.action, sizeof(wire.action));
    std::memcpy(decision.pos, wire.pos, sizeof(wire.pos));
    std::memcpy(decision.quat, wire.quat, sizeof(wire.quat));
    enemy_think::offer(wire.slot, wire.seq, decision);
}

struct Apply {
    void operator()(const Hit& hit) const { applyHit(hit); }
    void operator()(const enemy_protocol::Decision& decision) const { applyDecision(decision); }
};

void drain() {
    std::deque<Event> batch;
    {
        std::lock_guard lock(g_mutex);
        batch.swap(g_queue);
    }
    for (const Event& event : batch) std::visit(Apply{}, event);
}

void enqueue(const Event& event) {
    std::lock_guard lock(g_mutex);
    if (g_queue.size() == kMaxQueued) g_queue.pop_front();
    g_queue.push_back(event);
}

}  // namespace

namespace enemy_net {

void applyAsOwner(uintptr_t enemy, uint8_t attackerId, uintptr_t attackerObject, const game::HitPoint& point,
                  const game::HitInfo& info) {
    HitPayload hit;
    if (encode(enemy, attackerId, point, info, hit)) return runAsOwner(enemy, attackerObject, hit, point, info);
    game::HitPoint localPoint = point;
    game::HitInfo localInfo = info;
    enemy_damage_hook::runDamage(enemy, attackerObject, localPoint, localInfo);
}

bool requestHit(uintptr_t enemy, Character attacker, const game::HitPoint& point, const game::HitInfo& info) {
    HitPayload hit;
    if (!encode(enemy, static_cast<uint8_t>(attacker), point, info, hit)) return false;
    const bool sent = g_net->send(enemy_protocol::kMsgHitRequest, true, static_cast<uint8_t>(net_pad::peerSlot()),
                                  proto::bytesOf(hit));
    if (sent) debug_stats::count(Counter::HitRequestSent);
    return sent;
}

void sendDecision(uint8_t slot, const enemy_follow_rule::Decision& decision) {
    enemy_protocol::Decision wire{};
    if (!wireRoom(wire.room)) return;
    wire.slot = slot;
    wire.seq = g_decisionSeq++;
    std::memcpy(wire.action, decision.action.word, sizeof(wire.action));
    std::memcpy(wire.pos, decision.pos, sizeof(wire.pos));
    std::memcpy(wire.quat, decision.quat, sizeof(wire.quat));
    if (g_net->send(enemy_protocol::kMsgEnemyDecision, true, proto::kSlotAll, proto::bytesOf(wire))) {
        debug_stats::count(Counter::EnemyDecisionsSent);
    }
}

void onFrame(const GameFrame& frame) {
    if (frame.slot != net_pad::peerSlot()) return;
    if (frame.type == enemy_protocol::kMsgEnemyDecision && frame.payload.size() == sizeof(enemy_protocol::Decision)) {
        enemy_protocol::Decision decision;
        std::memcpy(&decision, frame.payload.data(), sizeof(decision));
        return enqueue(decision);
    }
    const bool hitType = frame.type == enemy_protocol::kMsgHitRequest || frame.type == enemy_protocol::kMsgHitApplied;
    if (!hitType || frame.payload.size() != sizeof(HitPayload)) return;
    debug_stats::count(frame.type == enemy_protocol::kMsgHitRequest ? Counter::HitRequestReceived
                                                                    : Counter::HitAppliedReceived);
    Hit hit{frame.type, {}};
    std::memcpy(&hit.payload, frame.payload.data(), sizeof(hit.payload));
    enqueue(hit);
}

void enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("enemy_net", drain);
}

}  // namespace enemy_net
