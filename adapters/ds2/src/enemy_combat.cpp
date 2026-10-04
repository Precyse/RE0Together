#include "enemy_combat.h"

#include <algorithm>
#include <map>

#include "authority_sync.h"
#include "authority_wire.h"
#include "combat_rules.h"
#include "combat_wire.h"
#include "enemy_directory.h"
#include "enemy_wire.h"
#include "game.h"
#include "reject_counters.h"
#include "time_us.h"

namespace {

using reject_counters::Reason;

constexpr uint64_t kEnemyObjectTag = uint64_t{0x45} << 56;  // 'E': an enemy's id in the authority table

// Net thread only.
game::CombatRole g_role = game::CombatRole::None;
uint8_t g_localSlot = 0;
uint8_t g_hostSlot = 0;
std::map<uint8_t, combat_rules::HitLimiter> g_limiters;  // per sending slot (host) or the host (guest)
combat_rules::DeathLedger g_handledDeaths;

uint64_t enemyObjectId(uint16_t netId) { return kEnemyObjectTag | netId; }

// The host runs every enemy unless the authority table says another slot does.
bool hostRunsEnemy(uint16_t netId) {
    const uint8_t owner = authority_sync::ownerOf(enemyObjectId(netId));
    return owner == authority_wire::kNoOwner || owner == g_localSlot;
}

void reject(uint16_t type, Reason reason) { reject_counters::count(type, reason); }

// Host: a guest's hit on one of the host's enemies.
void onEnemyHit(const GameFrame& frame) {
    combat_wire::EnemyHit hit;
    if (!combat_wire::decode(frame.payload, hit)) return reject(frame.type, Reason::Malformed);
    if (!g_limiters[frame.slot].allow(hit.enemy.netId, nowUs())) return reject(frame.type, Reason::RateLimited);
    if (!hostRunsEnemy(hit.enemy.netId)) return reject(frame.type, Reason::WrongSender);
    if (!game::enemyAlive(hit.enemy.uuid)) return reject(frame.type, Reason::UnknownObject);
    game::applyEnemyHit(hit);
}

// Guest: a host enemy's hit on the local player.
void onPlayerHit(const GameFrame& frame) {
    combat_wire::PlayerHit hit;
    if (!combat_wire::decode(frame.payload, hit)) return reject(frame.type, Reason::Malformed);
    if (!g_limiters[frame.slot].allow(hit.attacker.netId, nowUs())) return reject(frame.type, Reason::RateLimited);
    game::applyPlayerHit(hit);
}

// Guest: an enemy of the host died. Handled once per enemy, however often it is reported.
void onEnemyDeath(const GameFrame& frame) {
    combat_wire::EnemyDeath death;
    if (!combat_wire::decode(frame.payload, death)) return reject(frame.type, Reason::Malformed);
    if (g_handledDeaths.markFirst(death.enemy.netId)) game::killEnemy(death);
}

// Guest: the host's announcements name the enemies a hit on a puppet is sent for.
void onEnemyAnnouncement(const GameFrame& frame) {
    if (frame.type == enemy_wire::kMsgEnemySpawn) {
        enemy_wire::EnemySpawn spawn;
        if (!enemy_wire::decodeOne(frame.payload, spawn)) return;
        enemy_directory::Uuid uuid;
        std::copy(spawn.entityUuid, spawn.entityUuid + uuid.size(), uuid.begin());
        enemy_directory::set(spawn.netId, uuid);
    } else {
        enemy_wire::EnemyGone gone;
        if (enemy_wire::decodeOne(frame.payload, gone)) enemy_directory::forget(gone.netId);
    }
}

void setRole(game::CombatRole role) {
    if (role == g_role) return;
    if (role == game::CombatRole::Guest || g_role == game::CombatRole::Guest) enemy_directory::clear();
    g_role = role;
    g_limiters.clear();
    g_handledDeaths.clear();
    game::setCombatRole(role);
}

void sendOutgoing(NetClient& net) {
    for (const combat_wire::EnemyHit& hit : game::takeEnemyHits()) {
        net.send(combat_wire::kMsgEnemyHit, true, g_hostSlot, proto::bytesOf(hit));
    }
    for (const game::PlayerHitOut& out : game::takePlayerHits()) {
        net.send(combat_wire::kMsgPlayerHit, true, out.slot, proto::bytesOf(out.hit));
    }
    for (const combat_wire::EnemyDeath& death : game::takeEnemyDeaths()) {
        net.send(combat_wire::kMsgEnemyDeath, true, proto::kSlotAll, proto::bytesOf(death));
    }
}

}  // namespace

namespace enemy_combat {

void onFrame(const GameFrame& frame) {
    if (g_role == game::CombatRole::Host) {
        if (frame.type == combat_wire::kMsgEnemyHit) onEnemyHit(frame);
        return;
    }
    if (g_role != game::CombatRole::Guest || frame.slot != g_hostSlot) return;
    if (frame.type == combat_wire::kMsgPlayerHit) {
        onPlayerHit(frame);
    } else if (frame.type == combat_wire::kMsgEnemyDeath) {
        onEnemyDeath(frame);
    } else if (frame.type == enemy_wire::kMsgEnemySpawn || frame.type == enemy_wire::kMsgEnemyGone) {
        onEnemyAnnouncement(frame);
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const bool host = session.linked && session.localSlot == session.hostSlot;
    g_localSlot = session.localSlot;
    g_hostSlot = session.hostSlot;
    setRole(host ? game::CombatRole::Host : session.linked ? game::CombatRole::Guest : game::CombatRole::None);
    if (g_role != game::CombatRole::None) sendOutgoing(net);
}

}  // namespace enemy_combat
