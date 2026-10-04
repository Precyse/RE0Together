// DEATH STRANDING 2: combat between a guest and the host's enemies (docs/DS2_NOTES.md, "Enemy combat"). Every gameplay
// damage is queued by 0x140129de0 and applied by EntityManagerGame::ApplyDamage 0x1406fd570(manager, victim, params),
// which builds MsgDamage from the DamageParams and sends it to the victim's components. The hook on that one function:
// - guest, victim is a puppet (an enemy of the directory): the hit is not applied; its plain fields go to the host;
// - host, victim is the partner's body: the hit is not applied; its plain fields go to the partner.
// A received hit is applied on the simulation thread through the same function with the applying flag set, on
// DamageParams copied from the last real hit this machine saw (so every field the wire does not carry is the engine's
// own) with the wire's fields written over it and the attacker set to a local entity. A kill is the engine's own
// Entity::Kill. Enemies are held by UUID and resolved through the engine's entity map at every use.
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
#include "ds2/engine.h"
#include "ds2/entity_lookup.h"
#include "ds2/place.h"
#include "ds2/remote_player.h"
#include "ds2/sim_tick.h"
#include "enemy_directory.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "remote_apply.h"

namespace {

constexpr uintptr_t kApplyDamage = 0x1406fd570;  // EntityManagerGame vtable 0x14318a498 slot 3
constexpr uintptr_t kKill = 0x14013bef0;         // Entity::Kill(entity, attacker, params)
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
constexpr size_t kWeakNodeSize = 32;
constexpr uintptr_t kEntityWeakTarget = 0x20;  // a weak pointer to an entity points at entity + 0x20
constexpr uintptr_t kWeakSelf = 0x18;
constexpr size_t kVectorSize = sizeof(float) * 4;

constexpr ULONGLONG kDeathPollMs = 100;
constexpr ULONGLONG kKillHoldMs = 10000;  // a kill for a puppet that is not built yet waits this long
constexpr size_t kMaxQueued = 256;

using ApplyFn = void (*)(uintptr_t manager, uintptr_t victim, uintptr_t params);
using NodeFn = void (*)(uintptr_t node);
using KillFn = void (*)(uintptr_t entity, uintptr_t attacker, uintptr_t params);
ApplyFn g_apply = nullptr;

struct PendingKill {
    combat_wire::EnemyDeath death;
    ULONGLONG since;
};

std::atomic<game::CombatRole> g_role{game::CombatRole::None};
std::atomic<bool> g_roleChanged{false};

std::mutex g_mutex;  // guards everything below (the engine's threads divert hits, the net thread takes and gives)
alignas(16) uint8_t g_template[kDamageParamsSize];
bool g_haveTemplate = false;
std::vector<combat_wire::EnemyHit> g_hitsOut;
std::vector<game::PlayerHitOut> g_playerHitsOut;
std::vector<combat_wire::EnemyDeath> g_deathsOut;
std::vector<combat_wire::EnemyHit> g_hitsIn;
std::vector<combat_wire::PlayerHit> g_playerHitsIn;
std::vector<PendingKill> g_killsIn;

// Simulation thread only.
combat_rules::DeathLedger g_reported;

template <class T>
void push(std::vector<T>& queue, const T& item) {
    if (queue.size() < kMaxQueued) queue.push_back(item);
}

uintptr_t attackerOf(uintptr_t params) {
    const uintptr_t target = decima::readPointer(params + kParamsAttacker);
    return target ? target - kEntityWeakTarget : 0;
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
    return decima::safeCopy(&out.amount, params + kParamsAmount, sizeof(out.amount)) &&
           decima::safeCopy(&out.flags, params + kParamsFlags, sizeof(out.flags)) &&
           decima::safeCopy(&out.partIndex, params + kParamsPart, sizeof(out.partIndex)) &&
           decima::safeCopy(out.origin, params + kParamsOrigin, kVectorSize) &&
           decima::safeCopy(out.impulse, params + kParamsImpulse, kVectorSize) &&
           decima::safeCopy(out.normal, params + kParamsNormal, kVectorSize);
}

void writeFields(uint8_t* params, const combat_wire::HitFields& hit) {
    std::memcpy(params + kParamsAmount, &hit.amount, sizeof(hit.amount));
    std::memcpy(params + kParamsFlags, &hit.flags, sizeof(hit.flags));
    std::memcpy(params + kParamsPart, &hit.partIndex, sizeof(hit.partIndex));
    std::memcpy(params + kParamsOrigin, hit.origin, kVectorSize);
    std::memcpy(params + kParamsImpulse, hit.impulse, kVectorSize);
    std::memcpy(params + kParamsNormal, hit.normal, kVectorSize);
}

// Keeps the latest real parameters, without the attacker's weak pointer (it is relinked for each use).
void rememberParams(uintptr_t params) {
    alignas(16) uint8_t copy[kDamageParamsSize];
    if (!decima::safeCopy(copy, params, kDamageParamsSize)) return;
    std::memset(copy + kParamsAttacker, 0, kWeakNodeSize);
    std::lock_guard lock(g_mutex);
    std::memcpy(g_template, copy, kDamageParamsSize);
    g_haveTemplate = true;
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
    out.hit.attacker = refOf(attackerOf(params));
    if (readFields(params, out.hit.hit) && combat_wire::validHit(out.hit.hit)) {
        std::lock_guard lock(g_mutex);
        push(g_playerHitsOut, out);
    }
    return true;
}

void learnResource(uintptr_t params);

void applyDetour(uintptr_t manager, uintptr_t victim, uintptr_t params) {
    if (params && victim && !remote_apply::active()) {
        rememberParams(params);
        const game::CombatRole role = g_role;
        const bool diverted = (role == game::CombatRole::Guest && divertEnemyHit(victim, params)) ||
                              divertPlayerHit(victim, params, role == game::CombatRole::Host);
        if (diverted) return;
    }
    g_apply(manager, victim, params);
    if (params && victim == remote_player::samEntity() && !remote_apply::active()) learnResource(params);
}

// Links the attacker's weak pointer, applies, unlinks. False when the engine call faulted.
bool callApply(uintptr_t manager, uintptr_t victim, uintptr_t params, uintptr_t attacker) {
    const uintptr_t node = params + kParamsAttacker;
    if (attacker) {
        *reinterpret_cast<uintptr_t*>(node) = attacker + kEntityWeakTarget;
        *reinterpret_cast<uintptr_t*>(node + kWeakSelf) = node;
        reinterpret_cast<NodeFn>(ds2::at(kWeakLink))(node);
    }
    bool ok = true;
    __try {
        g_apply(manager, victim, params);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    if (attacker) reinterpret_cast<NodeFn>(ds2::at(kWeakUnlink))(node);
    return ok;
}

// Applies a hit to `victim` as if `attacker` had landed it. False when nothing could be applied.
bool applyHit(uintptr_t victim, uintptr_t attacker, const combat_wire::HitFields& hit) {
    alignas(16) uint8_t params[kDamageParamsSize];
    {
        std::lock_guard lock(g_mutex);
        if (!g_haveTemplate) return false;
        std::memcpy(params, g_template, kDamageParamsSize);
    }
    const uintptr_t manager = ds2::entityManager();
    if (!manager || !g_apply) return false;
    writeFields(params, hit);
    const remote_apply::Scope applying;
    return callApply(manager, victim, reinterpret_cast<uintptr_t>(params), attacker);
}

bool killGuarded(uintptr_t entity) {
    __try {
        reinterpret_cast<KillFn>(ds2::at(kKill))(entity, 0, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A forwarded hit is built the way the engine's script DealDamage builds one (0x14013b470, steps 0x14013b544..0x14013b689),
// except for the attack context's resource: a fresh top-level context (a new attack id, no source, so the damage gates'
// faction check is skipped) holds the default attack resource, which has no weapon damage profile, and a MULE takes no
// damage from it. The resource of the last real hit on the local player (a MULE's blow) is borrowed instead
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
constexpr uintptr_t kContextData = 0x120, kContextResource = 0x18, kContextType = 0x0C, kEventParent = 0x38;
constexpr uint32_t kFlagKind1 = 1u << 3, kFlagKind2 = 1u << 1;  // EDamageFlags bits of the hit kind
constexpr uint16_t kRefusedTypes[] = {0x023B, 0x0095};            // attack types the MULE damage gate refuses outright
constexpr ULONGLONG kWaitingMs = 30000;

struct DamageTypeArg {
    uint64_t type;
    uint32_t flags;
    uint32_t reserved;
};

std::atomic<uintptr_t> g_realResource{0};  // the attack resource of the last real hit on the local player
constexpr size_t kContextBlockCopy = 0xA0;  // the data block bytes borrowed from the real context (id and source are kept fresh)
alignas(16) uint8_t g_realBlock[kContextBlockCopy];
std::mutex g_blockMutex;
struct Waiting {
    combat_wire::EnemyHit hit;
    ULONGLONG since;
};
std::vector<Waiting> g_forwardWaiting;  // simulation thread only

uintptr_t parentContextOf(uintptr_t params) {
    const uintptr_t weak = decima::readPointer(params + kParamsAttacker);
    const uintptr_t event = weak ? weak - kEntityWeakTarget : 0;
    const uintptr_t parentWeak = event ? decima::readPointer(event + kEventParent) : 0;
    return parentWeak ? parentWeak - kEntityWeakTarget : 0;
}

// Remembers the attack resource of a real hit on the local player (called from the ApplyDamage detour).
void learnResource(uintptr_t params) {
    const uintptr_t parent = parentContextOf(params);
    if (!parent) return;
    const uint16_t type = ds2::field<uint16_t>(parent + kContextData, kContextType);
    if (type == 0) return;  // an attack of no kind (environment): its resource carries no weapon damage either
    for (const uint16_t refused : kRefusedTypes) {
        if (type == refused) return;
    }
    if (const uintptr_t resource = decima::readPointer(parent + kContextData + kContextResource)) {
        std::lock_guard lock(g_blockMutex);
        decima::safeCopy(g_realBlock, parent + kContextData, sizeof(g_realBlock));
        g_realResource = resource;
    }
}

void unlinkGuarded(uintptr_t node) {
    __try {
        reinterpret_cast<NodeFn>(ds2::at(kWeakUnlink))(node);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Copies the real context's data block into a fresh one, keeping the fresh attack id and "no source".
void borrowBlock(uintptr_t block) {
    constexpr uintptr_t kId = 0x08, kSource = 0x20;
    const uint64_t id = ds2::field<uint64_t>(block, kId), source = ds2::field<uint64_t>(block, kSource);
    {
        std::lock_guard lock(g_blockMutex);
        std::memcpy(reinterpret_cast<void*>(block), g_realBlock, sizeof(g_realBlock));
    }
    ds2::field<uint64_t>(block, kId) = id;
    ds2::field<uint64_t>(block, kSource) = source;
}

// Builds and queues one hit on `victim`. False when the engine faulted.
bool buildHit(uintptr_t victim, uintptr_t attacker, const combat_wire::HitFields& hit) {
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
        *reinterpret_cast<uintptr_t*>(node) = attacker + kEntityWeakTarget;
        *reinterpret_cast<uintptr_t*>(node + kWeakSelf) = node;
        reinterpret_cast<NodeFn>(ds2::at(kWeakLink))(node);
        linked = true;
        const uintptr_t link = reinterpret_cast<uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t)>(ds2::at(kMakeAttackLink))(0, 0, 0, desc);
        reinterpret_cast<void (*)(uintptr_t, uintptr_t, const void*, float, const float*, float, uint32_t)>(ds2::at(kInitDamageParams))(
            paramsAt, link, &type, hit.amount, direction, 0.0f, static_cast<uint32_t>(std::max(hit.partIndex, 0)));
        if (const uintptr_t context = parentContextOf(paramsAt)) borrowBlock(context + kContextData);
        std::memcpy(params + kParamsPosition, position, sizeof(position));
        params[kParamsHitFlag] = 1;
        if (hit.flags & kFlagKind1) ds2::field<uint32_t>(paramsAt, kParamsFlags) |= 8;
        if (hit.flags & kFlagKind2) ds2::field<uint32_t>(paramsAt, kParamsFlags) |= 2;
        reinterpret_cast<void (*)(uintptr_t, uintptr_t)>(ds2::at(kQueueDamageCall))(victim, paramsAt);
        reinterpret_cast<NodeFn>(ds2::at(kWeakUnlink))(paramsAt + kParamsAttacker);
        reinterpret_cast<NodeFn>(ds2::at(kWeakUnlink))(node);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (linked) unlinkGuarded(node);
        return false;
    }
}

void applyEnemyHitNow(const combat_wire::EnemyHit& hit) {
    if (g_forwardWaiting.size() < kMaxQueued) g_forwardWaiting.push_back({hit, GetTickCount64()});
}

// Simulation thread: forwarded hits wait until a real hit has shown the attack resource to borrow.
void runForwarded() {
    const uintptr_t resource = g_realResource.load();
    const ULONGLONG now = GetTickCount64();
    std::erase_if(g_forwardWaiting, [&](const Waiting& waiting) {
        if (!resource) return now - waiting.since > kWaitingMs;
        const uintptr_t enemy = ds2::entityByUuid(waiting.hit.enemy.uuid);
        if (!enemy || ds2::entityIsDead(enemy)) return true;
        const remote_apply::Scope applying;
        const bool ok = buildHit(enemy, remote_player::entity(), waiting.hit.hit);
        logger::write("enemy_combat: forwarded hit on enemy %u: %s", waiting.hit.enemy.netId, ok ? "queued" : "faulted");
        return true;
    });
}

void applyPlayerHitNow(const combat_wire::PlayerHit& hit) {
    const uintptr_t victim = remote_player::samEntity();
    if (!victim) return;
    const uintptr_t attacker = combat_wire::isNone(hit.attacker) ? 0 : ds2::entityByUuid(hit.attacker.uuid);
    if (!applyHit(victim, attacker, hit.hit)) logger::write("enemy_combat: could not apply an enemy's hit on the local player");
}

// Returns true when the kill is finished (done, or the enemy was already dead) and false while the puppet is not here.
bool killNow(const combat_wire::EnemyDeath& death) {
    const uintptr_t enemy = ds2::entityByUuid(death.enemy.uuid);
    if (!enemy) return false;
    if (!ds2::entityIsDead(enemy) && !killGuarded(enemy)) logger::write("enemy_combat: the kill of enemy %u faulted", death.enemy.netId);
    return true;
}

// Host: the directory's enemies that have died since they were last looked at.
void reportDeaths() {
    for (const enemy_directory::Entry& entry : enemy_directory::all()) {
        const uintptr_t enemy = ds2::entityByUuid(entry.uuid.data());
        if (!enemy || !ds2::entityIsDead(enemy) || !g_reported.markFirst(entry.netId)) continue;
        combat_wire::EnemyDeath death{};
        death.enemy.netId = entry.netId;
        std::copy(entry.uuid.begin(), entry.uuid.end(), death.enemy.uuid);
        std::lock_guard lock(g_mutex);
        push(g_deathsOut, death);
    }
}

void tickHost(std::vector<combat_wire::EnemyHit>& hits, ULONGLONG now) {
    static ULONGLONG lastPoll = 0;
    for (const combat_wire::EnemyHit& hit : hits) applyEnemyHitNow(hit);
    runForwarded();
    if (now - lastPoll < kDeathPollMs) return;
    lastPoll = now;
    reportDeaths();
}

void tickGuest(std::vector<combat_wire::PlayerHit>& hits, std::vector<PendingKill>& kills, ULONGLONG now) {
    for (const combat_wire::PlayerHit& hit : hits) applyPlayerHitNow(hit);
    std::erase_if(kills, [now](const PendingKill& pending) {
        if (killNow(pending.death)) return true;
        if (now - pending.since < kKillHoldMs) return false;
        logger::write("enemy_combat: enemy %u died on the host but has no puppet here", pending.death.enemy.netId);
        return true;
    });
}

// Simulation thread, ahead of the engine's object update.
void tick() {
    const game::CombatRole role = g_role;
    if (role == game::CombatRole::None) return;
    std::vector<combat_wire::EnemyHit> hits;
    std::vector<combat_wire::PlayerHit> playerHits;
    std::vector<PendingKill> kills;
    {
        std::lock_guard lock(g_mutex);
        hits.swap(g_hitsIn);
        playerHits.swap(g_playerHitsIn);
        kills.swap(g_killsIn);
    }
    if (g_roleChanged.exchange(false)) g_reported.clear();
    const ULONGLONG now = GetTickCount64();
    if (role == game::CombatRole::Host) {
        tickHost(hits, now);
        return;
    }
    tickGuest(playerHits, kills, now);
    std::lock_guard lock(g_mutex);  // the kills whose puppet is not built yet wait for the next frame
    g_killsIn.insert(g_killsIn.begin(), kills.begin(), kills.end());
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
    g_roleChanged = true;
    std::lock_guard lock(g_mutex);
    g_hitsOut.clear();
    g_playerHitsOut.clear();
    g_deathsOut.clear();
    g_hitsIn.clear();
    g_playerHitsIn.clear();
    g_killsIn.clear();
    logger::write("enemy_combat: role %s", role == CombatRole::Host ? "host" : role == CombatRole::Guest ? "guest" : "none");
}

std::vector<combat_wire::EnemyHit> takeEnemyHits() { return take(g_hitsOut); }
std::vector<PlayerHitOut> takePlayerHits() { return take(g_playerHitsOut); }
std::vector<combat_wire::EnemyDeath> takeEnemyDeaths() { return take(g_deathsOut); }

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

void killEnemy(const combat_wire::EnemyDeath& death) {
    std::lock_guard lock(g_mutex);
    push(g_killsIn, PendingKill{death, GetTickCount64()});
}

}  // namespace game
