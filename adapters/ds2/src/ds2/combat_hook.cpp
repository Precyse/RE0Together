// DEATH STRANDING 2: combat between a guest and the host's enemies (docs/DS2_NOTES.md, "Enemy combat"). Every gameplay
// damage is queued by 0x140129de0 and applied by EntityManagerGame::ApplyDamage 0x1406fd570(manager, victim, params),
// which builds MsgDamage from the DamageParams and sends it to the victim's components. The hook on that one function:
// - guest, victim is a puppet (an enemy of the directory): the hit is not applied; its plain fields go to the host;
// - host, victim is the partner's body: the hit is not applied; its plain fields go to the partner.
// A received hit is built the way the engine's script damage builds one (attack link, context, DamageParams) and queued on
// the simulation thread with the applying flag set, with the attacker set to a local entity. Enemies are held by UUID and resolved through the engine's entity map at every use.
#include "ds2/combat_hook.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

#include "combat_rules.h"
#include "combat_wire.h"
#include "decima/safe_read.h"
#include "ds2/combat_log.h"
#include "ds2/damage_params.h"
#include "ds2/engine.h"
#include "ds2/enemy_vitals.h"
#include "ds2/entity_lookup.h"
#include "ds2/place.h"
#include "ds2/remote_player.h"
#include "ds2/remote_weapon.h"
#include "ds2/sim_tick.h"
#include "enemy_directory.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "remote_apply.h"
#include "time_us.h"

namespace {

constexpr uintptr_t kApplyDamage = 0x1406fd570;  // EntityManagerGame vtable 0x14318a498 slot 3
constexpr uintptr_t kWeakLink = 0x142071f80;     // (WeakPtr node*): links a node whose target is set
constexpr uintptr_t kWeakUnlink = 0x142072000;   // (WeakPtr node*)

// DamageParams (0xB8 bytes, copy constructor 0x14011f120); the offsets are the plain fields combat_wire::HitFields carries.
constexpr size_t kDamageParamsSize = 0xB8;
constexpr uintptr_t kParamsOrigin = 0x00;
constexpr uintptr_t kParamsFlags = 0x08;
constexpr uintptr_t kParamsImpulse = 0x10;
constexpr uintptr_t kParamsAttacker = 0x20;  // a WeakPtr node {target = entity + 0x20, prev, next, self}
constexpr uintptr_t kParamsNormal = 0x50;
constexpr uintptr_t kParamsPart = 0x68;
constexpr uintptr_t kParamsAmount = 0x78;
constexpr uintptr_t kEntityWeakTarget = 0x20;  // a weak pointer to an entity points at entity + 0x20
constexpr uintptr_t kWeakSelf = 0x18;
constexpr size_t kVectorSize = sizeof(float) * 4;

constexpr size_t kMaxQueued = 256;
constexpr float kMinForwardedAmount = 0.5f;  // contact and physics touches carry fractions of a point; they are dropped, not sent

using ApplyFn = void (*)(uintptr_t manager, uintptr_t victim, uintptr_t params);
using NodeFn = void (*)(uintptr_t node);
ApplyFn g_apply = nullptr;

std::atomic<game::CombatRole> g_role{game::CombatRole::None};

std::mutex g_mutex;  // guards everything below (the engine's threads divert hits, the net thread takes and gives)
std::vector<combat_wire::EnemyHit> g_hitsOut;
std::vector<game::PlayerHitOut> g_playerHitsOut;
std::vector<combat_wire::EnemyHit> g_hitsIn;
std::vector<combat_wire::PlayerHit> g_playerHitsIn;

combat_rules::HitLimiter g_playerHitLimiter;  // the partner's hits sent per attacker, so a continuous source cannot flood the wire

template <class T>
void push(std::vector<T>& queue, const T& item) {
    if (queue.size() < kMaxQueued) queue.push_back(item);
}

combat_wire::EnemyRef refOf(uintptr_t entity) {
    combat_wire::EnemyRef ref{};
    enemy_directory::Uuid uuid;
    if (!entity || !ds2::entityUuid(entity, uuid)) return ref;
    std::copy(uuid.begin(), uuid.end(), ref.uuid);
    ref.netId = enemy_directory::netIdOf(uuid);
    return ref;
}

bool readFields(uintptr_t params, combat_wire::HitFields& out) {
    out = {};
    out.amount = ds2::damage::amountOf(params);
    return decima::safeCopy(&out.flags, params + kParamsFlags, sizeof(out.flags)) &&
           decima::safeCopy(&out.partIndex, params + kParamsPart, sizeof(out.partIndex)) &&
           decima::safeCopy(out.origin, params + kParamsOrigin, kVectorSize) &&
           decima::safeCopy(out.impulse, params + kParamsImpulse, kVectorSize) &&
           decima::safeCopy(out.normal, params + kParamsNormal, kVectorSize);
}

bool divertEnemyHit(uintptr_t victim, uintptr_t params) {
    const combat_wire::EnemyRef enemy = refOf(victim);
    if (enemy.netId == combat_wire::kNoEnemy) return false;
    combat_wire::EnemyHit hit{};
    hit.enemy = enemy;
    if (readFields(params, hit.hit) && combat_wire::validHit(hit.hit)) {
        std::lock_guard lock(g_mutex);
        push(g_hitsOut, hit);
    }
    return true;  // a puppet never takes damage here, whatever the hit was
}

// The remote body stands for the partner: its own game decides what hurts the partner, so nothing may hurt the body here.
// On the host, hits from enemies are forwarded to the partner; any other hit (drowning, a fall, timefall) is only dropped.
bool divertPlayerHit(uintptr_t victim, uintptr_t params, bool forward) {
    const uintptr_t remote = remote_player::entity();
    if (!remote || victim != remote) return false;
    if (!forward) return true;
    game::PlayerHitOut out{remote_player::slot(), {}};
    out.hit.attacker = refOf(ds2::damage::attackerOf(params));
    if (readFields(params, out.hit.hit) && combat_wire::validHit(out.hit.hit) && out.hit.hit.amount >= kMinForwardedAmount) {
        std::lock_guard lock(g_mutex);
        if (g_playerHitLimiter.allow(out.hit.attacker.netId, nowUs())) push(g_playerHitsOut, out);
    }
    return true;
}

// A forwarded hit is built the way the engine's script DealDamage builds one (0x14013b470, steps 0x14013b544..0x14013b689),
// except for the attack context: a fresh context holds the default attack resource and type 0, which the NPC damage
// handler does not turn into damage. The data block of a real hit of the same kind is copied instead (a real hit by the
// local player on an enemy for a partner's hit on an enemy, an enemy's blow on the local player for a hit on the local
// player), with a fresh attack id, and a hit on an enemy carries no amount, as a real weapon hit does
// (docs/DS2_NOTES.md, "Enemy combat").
constexpr uintptr_t kMakeAttackLink = 0x140210ad0;    // (0, 0, 0, descriptor*) -> link; link +0x38 is a weak ref to the context
constexpr uintptr_t kInitDamageParams = 0x1401c5120;  // (params, link, {u64 type, u32 flags}*, f32 amount, f32[4]* direction, f32 amount2, u32 part)
constexpr uintptr_t kQueueDamageCall = 0x140129de0;   // (victim, params*): copies the parameters
constexpr uintptr_t kDescriptorVtable = 0x143127A58;
constexpr uintptr_t kDefaultDamageType = 0x14627E4D8;  // DamageTypeResourceSettings.DefaultDamageTypeResource
constexpr uint8_t kDescriptorSourceKind = 7;
constexpr size_t kDescriptorSize = 0x40;  // the weak node (32 bytes) sits at +0x10
constexpr uintptr_t kDescKind = 0x08, kDescWeak = 0x10;
constexpr uintptr_t kParamsPosition = 0x98, kParamsHitFlag = 0xB0;
constexpr uintptr_t kContextData = ds2::damage::kContextData, kContextResource = ds2::damage::kDataResource,
                    kContextType = ds2::damage::kDataType;
constexpr uint32_t kFlagKind1 = 1u << 3, kFlagKind2 = 1u << 1;  // EDamageFlags bits of the hit kind
constexpr uint16_t kRefusedTypes[] = {0x023B, 0x0095};            // attack types the MULE damage gate refuses outright
constexpr ULONGLONG kWaitingMs = 30000;
// An enemy the host's streaming has put to sleep (entity flags +0x98 bit 9) is sent no messages, so a hit on it never reaches its
// damage component; the partner can be fighting it while the host's own player is far away, so the hit waits while the
// enemy is woken (Entity wake 0x1401312b0(entity, 0, 0) sets a wake request, flags +0x98 bit 36, that the engine serves on
// a later update and which clears bit 9).
constexpr uintptr_t kEntityFlags = 0x98, kWakeEntity = 0x1401312b0;
constexpr unsigned kAsleepBit = 9;

struct DamageTypeArg {
    uint64_t type;
    uint32_t flags;
    uint32_t reserved;
};

// A weapon's bullet reaches an enemy as two hits, an impact of type 0x23b and a damage hit of type 0xd (live: only the
// second changes the enemy's health), so the partner's hits on enemies are built as the second: type 0xd, the weapon's
// bullet attack resource, source 0 and an id of the real form (0xb000d in the bits above 32, a counter below).
constexpr uint16_t kBulletAttackType = 0x0D;
constexpr uint64_t kBulletAttackIdPrefix = 0x0B000D;
constexpr uint64_t kAttackIdCounterBits = 32;
constexpr size_t kContextBlockCopy = 0xA0;  // the data block bytes borrowed from a real context
constexpr uint64_t kAttackIdCounterMask = 0xFFFFFF;  // the low 24 bits of an attack id count up (1d023b000002c6: type 0x23b above, counter 0x2c6); the rest names the attack

// The data block of a real attack context and its resource, kept to build the same kind of hit.
struct LearnedAttack {
    mutable std::mutex mutex;
    alignas(16) uint8_t block[kContextBlockCopy] = {};
    std::atomic<uintptr_t> resource{0};
};
LearnedAttack g_onPlayer;  // an enemy's blow on the local player: the template for hits on the local player
struct Waiting {
    combat_wire::EnemyHit hit;
    ULONGLONG since;
    bool wakeRequested = false;
};
std::vector<Waiting> g_forwardWaiting;  // simulation thread only

// Remembers the context data block of a real hit (called from the ApplyDamage detour).
void learn(LearnedAttack& into, uintptr_t params) {
    const uintptr_t parent = ds2::damage::contextOf(params);
    if (!parent) return;
    const uint16_t type = ds2::field<uint16_t>(parent + kContextData, kContextType);
    if (type == 0) return;  // an attack of no kind (environment): its resource carries no weapon damage either
    for (const uint16_t refused : kRefusedTypes) {
        if (type == refused) return;
    }
    if (const uintptr_t resource = decima::readPointer(parent + kContextData + kContextResource)) {
        std::lock_guard lock(into.mutex);
        decima::safeCopy(into.block, parent + kContextData, sizeof(into.block));
        into.resource = resource;
    }
}

void applyDetour(uintptr_t manager, uintptr_t victim, uintptr_t params) {
    const combat_log::Snapshot snapshot = combat_log::before(victim, params);
    if (params && victim && !remote_apply::active()) {
        const game::CombatRole role = g_role;
        const bool diverted = (role == game::CombatRole::Guest && divertEnemyHit(victim, params)) ||
                              divertPlayerHit(victim, params, role == game::CombatRole::Host);
        if (diverted) {
            combat_log::after(snapshot, victim, params, "diverted, not applied here");
            return;
        }
    }
    g_apply(manager, victim, params);
    combat_log::after(snapshot, victim, params, "applied");
    if (!params || remote_apply::active()) return;
    if (victim == remote_player::samEntity()) learn(g_onPlayer, params);
}

void unlinkGuarded(uintptr_t node) {
    __try {
        reinterpret_cast<NodeFn>(ds2::at(kWeakUnlink))(node);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Copies the learned context data block of an enemy's blow into a fresh one with a fresh attack id (the learned id's
// upper bits and the fresh counter: an id seen twice within the cooldown is refused), keeping the fresh "no source".
void borrowBlock(const LearnedAttack& from, uintptr_t block) {
    constexpr uintptr_t kId = 0x08, kSource = 0x20;
    const uint64_t freshId = ds2::field<uint64_t>(block, kId), freshSource = ds2::field<uint64_t>(block, kSource);
    uint64_t learnedId = 0;
    {
        std::lock_guard lock(from.mutex);
        std::memcpy(reinterpret_cast<void*>(block), from.block, sizeof(from.block));
        std::memcpy(&learnedId, from.block + kId, sizeof(learnedId));
    }
    ds2::field<uint64_t>(block, kId) = (learnedId & ~kAttackIdCounterMask) | (freshId & kAttackIdCounterMask);
    ds2::field<uint64_t>(block, kSource) = freshSource;
}

// Makes the context a weapon's damage hit of the given bullet attack resource (see kBulletAttackType).
void makeBulletContext(uintptr_t block, uintptr_t resource) {
    constexpr uintptr_t kId = 0x08, kSource = 0x20;
    const uint64_t counter = ds2::field<uint64_t>(block, kId) & ((1ull << kAttackIdCounterBits) - 1);
    ds2::field<uint64_t>(block, kId) = (kBulletAttackIdPrefix << kAttackIdCounterBits) | counter;
    ds2::field<uint16_t>(block, kContextType) = kBulletAttackType;
    ds2::field<uint64_t>(block, kContextResource) = resource;
    ds2::field<uint64_t>(block, kSource) = 0;
}

// Builds and queues one hit on `victim`. False when the engine faulted.
// A hit on the local player is built from an enemy's learned blow; any other victim takes a weapon's bullet damage hit with
// bulletResource.
bool buildHit(uintptr_t victim, uintptr_t attacker, const combat_wire::HitFields& hit, uintptr_t bulletResource = 0) {
    const bool onPlayer = victim == remote_player::samEntity();
    // A real weapon hit carries no amount in its parameters (the engine works it out from the attack's resource), so a
    // hit on an enemy is built the same way; a hit on the local player keeps the amount it was sent with.
    const float amount = onPlayer ? hit.amount : 0.0f;
    decima::WorldTransform where;
    if (!ds2::entityTransform(victim, where)) return false;
    alignas(16) uint8_t descriptor[kDescriptorSize] = {};
    alignas(16) uint8_t params[kDamageParamsSize] = {};
    alignas(16) float direction[4];
    std::memcpy(direction, hit.impulse, sizeof(direction));
    if (direction[0] == 0 && direction[1] == 0 && direction[2] == 0) direction[1] = 1.0f;
    const DamageTypeArg type{decima::readPointer(ds2::at(kDefaultDamageType)), 0, 0};
    const double position[3] = {where.position.x, where.position.y, where.position.z};
    const uintptr_t desc = reinterpret_cast<uintptr_t>(descriptor);
    const uintptr_t node = desc + kDescWeak;
    const uintptr_t paramsAt = reinterpret_cast<uintptr_t>(params);
    bool linked = false;
    __try {
        *reinterpret_cast<uintptr_t*>(desc) = ds2::at(kDescriptorVtable);
        descriptor[kDescKind] = kDescriptorSourceKind;
        if (attacker) {  // a hit with no attacker (the environment) leaves the descriptor's weak node empty
            *reinterpret_cast<uintptr_t*>(node) = attacker + kEntityWeakTarget;
            *reinterpret_cast<uintptr_t*>(node + kWeakSelf) = node;
            reinterpret_cast<NodeFn>(ds2::at(kWeakLink))(node);
            linked = true;
        }
        const uintptr_t link = reinterpret_cast<uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t)>(ds2::at(kMakeAttackLink))(0, 0, 0, desc);
        reinterpret_cast<void (*)(uintptr_t, uintptr_t, const void*, float, const float*, float, uint32_t)>(ds2::at(kInitDamageParams))(
            paramsAt, link, &type, amount, direction, 0.0f, static_cast<uint32_t>(std::max(hit.partIndex, 0)));
        if (onPlayer) std::memcpy(params + kParamsAmount, &amount, sizeof(amount));
        if (const uintptr_t context = ds2::damage::contextOf(paramsAt)) {
            if (!onPlayer) {
                makeBulletContext(context + kContextData, bulletResource);
            } else if (g_onPlayer.resource.load()) {
                borrowBlock(g_onPlayer, context + kContextData);
            }
        }
        std::memcpy(params + kParamsPosition, position, sizeof(position));
        params[kParamsHitFlag] = 1;
        if (hit.flags & kFlagKind1) ds2::field<uint32_t>(paramsAt, kParamsFlags) |= 8;
        if (hit.flags & kFlagKind2) ds2::field<uint32_t>(paramsAt, kParamsFlags) |= 2;
        reinterpret_cast<void (*)(uintptr_t, uintptr_t)>(ds2::at(kQueueDamageCall))(victim, paramsAt);
        reinterpret_cast<NodeFn>(ds2::at(kWeakUnlink))(paramsAt + kParamsAttacker);
        if (linked) reinterpret_cast<NodeFn>(ds2::at(kWeakUnlink))(node);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (linked) unlinkGuarded(node);
        return false;
    }
}

void applyEnemyHitNow(const combat_wire::EnemyHit& hit) {
    if (g_forwardWaiting.size() < kMaxQueued) g_forwardWaiting.push_back({hit, GetTickCount64()});
}

bool isAsleep(uintptr_t entity) { return (ds2::field<uint64_t>(entity, kEntityFlags) >> kAsleepBit) & 1; }

bool wakeGuarded(uintptr_t entity) {
    __try {
        reinterpret_cast<void (*)(uintptr_t, uintptr_t, uintptr_t)>(ds2::at(kWakeEntity))(entity, 0, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Simulation thread: the partner's hits on enemies, built with the bullet attack resource of the weapon the body holds.
void runForwarded() {
    const uintptr_t resource = remote_weapon::attackResource();
    const ULONGLONG now = GetTickCount64();
    std::erase_if(g_forwardWaiting, [&](Waiting& waiting) {
        if (!resource) return now - waiting.since > kWaitingMs;  // the body's weapon may still be on its way
        const uintptr_t enemy = ds2::entityByUuid(waiting.hit.enemy.uuid);
        if (!enemy || ds2::entityIsDead(enemy)) return true;
        if (isAsleep(enemy)) {
            if (!waiting.wakeRequested) {
                waiting.wakeRequested = true;
                logger::write("enemy_combat: enemy %u is asleep on the host, wake %s", waiting.hit.enemy.netId,
                              wakeGuarded(enemy) ? "requested for the partner's hit" : "faulted");
            }
            return now - waiting.since > kWaitingMs;  // the hit waits while the engine wakes the enemy
        }
        const remote_apply::Scope applying;
        const bool ok = buildHit(enemy, remote_player::entity(), waiting.hit.hit, resource);
        logger::write("enemy_combat: forwarded hit on enemy %u (entity %p, health %u of 254, attack resource %p): %s",
                      waiting.hit.enemy.netId, reinterpret_cast<void*>(enemy), enemy_vitals::readHealth(enemy),
                      reinterpret_cast<void*>(resource), ok ? "queued" : "faulted");
        return true;
    });
}

void applyPlayerHitNow(const combat_wire::PlayerHit& hit) {
    const uintptr_t victim = remote_player::samEntity();
    if (!victim) return;
    const uintptr_t attacker = combat_wire::isNone(hit.attacker) ? 0 : ds2::entityByUuid(hit.attacker.uuid);
    const remote_apply::Scope applying;
    if (!buildHit(victim, attacker, hit.hit)) logger::write("enemy_combat: could not apply an enemy's hit on the local player");
}

void tickHost(const std::vector<combat_wire::EnemyHit>& hits) {
    for (const combat_wire::EnemyHit& hit : hits) applyEnemyHitNow(hit);
    runForwarded();
}

void tickGuest(const std::vector<combat_wire::PlayerHit>& hits) {
    for (const combat_wire::PlayerHit& hit : hits) applyPlayerHitNow(hit);
}

// Simulation thread, ahead of the engine's object update.
void tick() {
    const game::CombatRole role = g_role;
    if (role == game::CombatRole::None) return;
    std::vector<combat_wire::EnemyHit> hits;
    std::vector<combat_wire::PlayerHit> playerHits;
    {
        std::lock_guard lock(g_mutex);
        hits.swap(g_hitsIn);
        playerHits.swap(g_playerHitsIn);
    }
    if (role == game::CombatRole::Host) {
        tickHost(hits);
    } else {
        tickGuest(playerHits);
    }
}

template <class T>
std::vector<T> take(std::vector<T>& queue) {
    std::lock_guard lock(g_mutex);
    std::vector<T> out;
    out.swap(queue);
    return out;
}

}  // namespace

namespace combat_hook {

void installEarly() {
    hooks::install("combat damage", ds2::at(kApplyDamage), reinterpret_cast<void*>(&applyDetour),
                   reinterpret_cast<void**>(&g_apply));
    sim_tick::add(&tick, "combat");
}

}  // namespace combat_hook

namespace game {

void setCombatRole(CombatRole role) {
    if (g_role.exchange(role) == role) return;
    std::lock_guard lock(g_mutex);
    g_hitsOut.clear();
    g_playerHitsOut.clear();
    g_hitsIn.clear();
    g_playerHitsIn.clear();
    logger::write("enemy_combat: role %s", role == CombatRole::Host ? "host" : role == CombatRole::Guest ? "guest" : "none");
}

std::vector<combat_wire::EnemyHit> takeEnemyHits() { return take(g_hitsOut); }
std::vector<PlayerHitOut> takePlayerHits() { return take(g_playerHitsOut); }

bool enemyAlive(const uint8_t (&uuid)[enemy_wire::kUuidSize]) {
    const uintptr_t enemy = ds2::entityByUuid(uuid);
    return enemy && !ds2::entityIsDead(enemy);
}

void applyEnemyHit(const combat_wire::EnemyHit& hit) {
    std::lock_guard lock(g_mutex);
    push(g_hitsIn, hit);
}

void applyPlayerHit(const combat_wire::PlayerHit& hit) {
    std::lock_guard lock(g_mutex);
    push(g_playerHitsIn, hit);
}

}  // namespace game
