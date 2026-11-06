#include "enemy_mirror.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <vector>

#include "calls.h"
#include "enemy_rule.h"
#include "enemy_spawn.h"
#include "local_body.h"
#include "log.h"
#include "object_set.h"
#include "peer_bodies.h"
#include "puppet_driver.h"
#include "puppet_registry.h"
#include "refw.h"
#include "roster.h"
#include "session.h"
#include "wire_dd2.h"
#include "world_gate.h"

namespace {

using roster_rule::Key;
using world_gate_rule::Place;

constexpr uint64_t kActionPeriodMs = 250;  // one removal or creation per period: 4 per second
constexpr uint64_t kSummaryPeriodMs = 30000;
constexpr float kHpEpsilon = 0.5f;
constexpr bool kOnlyWhileUp = false;       // setHp's Boolean: not on a region that is already down
constexpr uint32_t kWholeBody = 0;         // setHp's region index
// DamageInfo (static_checks.md P7-5; HitInfo base fields).
constexpr uint32_t kInfoAttackHit = 0xA0;       // <AttackHitController>
constexpr uint32_t kInfoAttackOwnerHit = 0xB8;  // <AttackOwnerHitController> (the shooter of an arrow)
constexpr uint32_t kInfoDamage = 0x1A0;
constexpr uint32_t kInfoReaction = 0x128;
constexpr uint32_t kInfoReactionLevel = 0x1B0;
constexpr uint32_t kInfoDamageType = 0x18C;

struct Mirror {
    puppet_registry::Body body;
    std::vector<const void*> extraObjects;  // the enemy's own navigation, the monster's selector
    puppet_driver::State driver;
    std::array<uint32_t, local_body::kSampledLayers> lastAction{};
};

struct HostState {
    wire_dd2::EnemyState state;
    uint64_t atMs = 0;
};

struct Captured {
    void* hit = nullptr;  // the mirror's HitController
    wire_dd2::EnemyHit message{};
};

bool g_enabled = false;
ObjectSet g_objects;
std::map<Key, Mirror> g_mirrors;
std::map<Key, HostState> g_host;
std::set<Key> g_killed, g_destroyed, g_wanted, g_created;  // this load of ours
std::deque<wire_dd2::Spawn> g_spawns;
std::optional<uint16_t> g_load;
uint64_t g_lastActionMs = 0, g_lastSummaryMs = 0;
uint32_t g_kills = 0, g_removed = 0, g_createdCount = 0, g_hitsSent = 0;

std::array<std::atomic<void*>, wire_dd2::kBodyKinds> g_ownHits{};  // our Arisen's and main pawn's HitControllers
std::mutex g_capturedMutex;
std::vector<Captured> g_captured;  // under g_capturedMutex

void* asArg(uint64_t value) { return reinterpret_cast<void*>(static_cast<uintptr_t>(value)); }

void* floatArg(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return asArg(bits);
}

void rebuildObjects() {
    std::set<const void*> objects;
    for (const auto& [key, mirror] : g_mirrors) {
        for (const void* object : {mirror.body.character, mirror.body.actions, mirror.body.selector, mirror.body.navigation,
                                   mirror.body.hit, mirror.body.turn}) {
            if (object) objects.insert(object);
        }
        for (const void* object : mirror.extraObjects) {
            if (object) objects.insert(object);
        }
    }
    g_objects.replace(std::move(objects));
}

void unbindAll() {
    if (g_mirrors.empty()) return;
    g_mirrors.clear();
    rebuildObjects();
}

void resetLoad() {
    unbindAll();
    g_host.clear();
    g_killed.clear();
    g_destroyed.clear();
    g_wanted.clear();
    g_created.clear();
    g_spawns.clear();
}

Mirror mirrorOf(const roster::Member& member) {
    void* character = member.character;
    Mirror mirror;
    mirror.body = {character,
                   calls::objectResult(calls::kGetActionManager, character),
                   member.monster ? nullptr : calls::objectResult(calls::kGetHumanActionSelector, character),
                   calls::objectResult(calls::kGetNavigationController, character),
                   calls::objectResult(calls::kGetHit, character),
                   calls::objectResult(calls::kGetTargetAngleCtrl, character)};
    mirror.extraObjects.push_back(member.monster ? calls::objectResult(calls::kGetMonsterSelector, member.component)
                                                 : calls::objectResult(calls::kGetEnemyNavigation, member.component));
    return mirror;
}

enemy_rule::Point pointOf(const double position[3]) { return {position[0], position[1], position[2]}; }

bool nearHost(const enemy_rule::Point& enemy, const enemy_rule::Point& host) {
    return enemy_rule::streamed(enemy, std::span(&host, 1));
}

// The host reports this enemy now, within the stream range of the host's own body: ours to mirror (or, missing, to create).
bool hostOwns(const Key& key, const enemy_rule::Point& host, uint64_t nowMs) {
    const auto state = g_host.find(key);
    return state != g_host.end() && enemy_rule::fresh(nowMs, state->second.atMs) &&
           nearHost(pointOf(state->second.state.position), host);
}

// The keys in both rosters that the host owns are mirrors; any other mirror is let go (AI and damage back to normal).
void bind(const std::vector<Key>& both, const std::vector<roster::Member>& members) {
    bool changed = false;
    for (auto it = g_mirrors.begin(); it != g_mirrors.end();) {
        if (!std::binary_search(both.begin(), both.end(), it->first)) {
            it = g_mirrors.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    for (const roster::Member& member : members) {
        if (g_mirrors.count(member.key) || g_killed.count(member.key) ||
            !std::binary_search(both.begin(), both.end(), member.key)) {
            continue;
        }
        g_mirrors.emplace(member.key, mirrorOf(member));
        changed = true;
    }
    if (changed) rebuildObjects();
}

// The host's enemy died: ours dies the engine's own way (its death, XP and drops here), unbound first so the HP gate
// lets the lethal hit through.
void kill(std::map<Key, Mirror>::iterator it) {
    void* character = it->second.body.character;
    g_killed.insert(it->first);
    g_mirrors.erase(it);
    rebuildObjects();
    if (refw::invoke(calls::method(calls::kKillAndSetDieLoop), character, {nullptr})) ++g_kills;
}

void syncHp(const Mirror& mirror, float hostHp) {
    const refw::InvokeResult* hp = refw::invoke(calls::method(calls::kGetHp), mirror.body.character);
    if (!hp || !mirror.body.hit || std::fabs(hp->f - hostHp) <= kHpEpsilon || hostHp <= 0.0f) return;
    const puppet_registry::DriverScope scope;
    refw::invoke(calls::method(calls::kSetHp), mirror.body.hit,
                 {floatArg(hostHp), asArg(kOnlyWhileUp), asArg(kWholeBody)});
}

void drive() {
    for (auto it = g_mirrors.begin(); it != g_mirrors.end();) {
        const wire_dd2::EnemyState& state = g_host.at(it->first).state;  // bind() kept only keys with a fresh state
        if (state.flags & wire_dd2::kEnemyDead) {
            kill(it++);
            continue;
        }
        Mirror& mirror = it->second;
        wire_dd2::BodyState target{};
        std::copy(std::begin(state.position), std::end(state.position), target.position);
        target.yaw = state.yaw;
        target.speedType = static_cast<float>(state.speedType);
        std::vector<wire_dd2::ActionEvent> edges;
        for (uint8_t layer = 0; layer < local_body::kSampledLayers; ++layer) {
            if (state.actionHash[layer] && state.actionHash[layer] != mirror.lastAction[layer]) {
                wire_dd2::ActionEvent edge{};
                edge.layer = layer;
                edge.actionHash = state.actionHash[layer];
                edges.push_back(edge);
            }
            mirror.lastAction[layer] = state.actionHash[layer];
        }
        puppet_driver::drive(mirror.body, target, edges, mirror.driver);
        syncHp(mirror, state.hp);
        ++it;
    }
}

const roster::Member* memberWith(const std::vector<roster::Member>& members, const Key& key) {
    for (const roster::Member& member : members) {
        if (member.key == key) return &member;
    }
    return nullptr;
}

bool memberNearHost(const roster::Member& member, const enemy_rule::Point& host) {
    const auto sample = local_body::sampleCharacter(member.character);
    return sample && nearHost(pointOf(sample->position), host);
}

// One removal of an extra (only within the host's range: farther out it stays ours) or one creation of a missing enemy
// per period.
void reconcile(const roster_rule::Comparison& comparison, const std::vector<roster::Member>& members,
               const enemy_rule::Point& host, uint64_t nowMs) {
    if (nowMs - g_lastActionMs < kActionPeriodMs) return;
    for (const Key& key : comparison.extra) {
        const roster::Member* member = memberWith(members, key);
        if (!member || g_destroyed.count(key) || !memberNearHost(*member, host)) continue;
        g_destroyed.insert(key);
        g_lastActionMs = nowMs;
        if (enemy_spawn::destroy(member->character)) ++g_removed;
        return;
    }
    while (!g_spawns.empty()) {
        const wire_dd2::Spawn spawn = std::move(g_spawns.front());
        g_spawns.pop_front();
        const bool stillMissing = std::binary_search(comparison.missing.begin(), comparison.missing.end(), spawn.key);
        if (!stillMissing || !g_created.insert(spawn.key).second) continue;
        g_lastActionMs = nowMs;
        if (enemy_spawn::create(spawn.json)) ++g_createdCount;
        return;
    }
}

void askForMissing(const std::vector<Key>& missing) {
    std::vector<Key> fresh;
    for (const Key& key : missing) {
        if (!g_wanted.count(key) && fresh.size() < wire_dd2::kMaxRosterKeys) fresh.push_back(key);
    }
    if (fresh.empty() || !session::send(wire_dd2::kEnemyWant, true, wire_dd2::encodeRoster(world_gate::loadSerial(), fresh))) return;
    g_wanted.insert(fresh.begin(), fresh.end());
}

void refreshOwnHits() {
    for (uint8_t kind = 0; kind < wire_dd2::kBodyKinds; ++kind) {
        void* character = local_body::character(kind);
        g_ownHits[kind] = character ? calls::objectResult(calls::kGetHit, character) : nullptr;
    }
}

void sendHits() {
    std::vector<Captured> captured;
    {
        std::lock_guard lock(g_capturedMutex);
        captured.swap(g_captured);
    }
    for (Captured& hit : captured) {
        for (const auto& [key, mirror] : g_mirrors) {
            if (mirror.body.hit != hit.hit) continue;
            hit.message.key = key;
            if (session::send(wire_dd2::kEnemyHit, true, wire_dd2::encode(hit.message))) ++g_hitsSent;
            break;
        }
    }
}

void summary(uint64_t nowMs) {
    if (nowMs - g_lastSummaryMs < kSummaryPeriodMs) return;
    g_lastSummaryMs = nowMs;
    if (g_mirrors.empty() && !g_kills && !g_removed && !g_createdCount && !g_hitsSent) return;
    logger::write("enemy_mirror: %zu mirrors; in 30 s %u killed with the host, %u extras removed, %u created, %u hits sent",
                  g_mirrors.size(), g_kills, g_removed, g_createdCount, g_hitsSent);
    g_kills = g_removed = g_createdCount = g_hitsSent = 0;
}

// A stale host (session::hostPlace is empty then) counts as not in game: every mirror is released.
bool bothInGame() {
    const auto host = session::hostPlace();
    return world_gate::place() == Place::InGame && host && static_cast<Place>(host->place) == Place::InGame;
}

}  // namespace

namespace enemy_mirror {

void setEnabled(bool on) { g_enabled = on; }

void tick(uint64_t nowMs) {
    if (!g_enabled) return;
    refreshOwnHits();
    summary(nowMs);
    if (g_load != world_gate::loadSerial()) {
        g_load = world_gate::loadSerial();
        resetLoad();
    }
    const std::vector<Key>* hostKeys = roster::hostKeys();
    const auto hostSlot = session::hostSlot();
    const auto hostBody = hostSlot ? peer_bodies::latest(*hostSlot, wire_dd2::kKindArisen) : std::nullopt;
    if (session::isHost() || !session::active() || !bothInGame() || !hostKeys || !hostBody) {
        unbindAll();
        return;
    }
    const enemy_rule::Point host = pointOf(hostBody->position);
    const std::vector<roster::Member> members = roster::members();
    std::vector<Key> own;
    own.reserve(members.size());
    for (const roster::Member& member : members) own.push_back(member.key);
    roster_rule::Comparison comparison = roster_rule::compare(*hostKeys, own);
    const auto notHostOwned = [&](const Key& key) { return !hostOwns(key, host, nowMs); };
    std::erase_if(comparison.both, notHostOwned);
    std::erase_if(comparison.missing, notHostOwned);
    bind(comparison.both, members);
    drive();
    sendHits();
    if (!roster::settled(nowMs)) return;
    askForMissing(comparison.missing);
    reconcile(comparison, members, host, nowMs);
}

bool isMirrorObject(const void* object) { return g_objects.contains(object); }

void onDamage(void* hitController, void* info) {
    if (!info) return;
    void* attacker = refw::field<void*>(info, kInfoAttackOwnerHit);
    if (!attacker) attacker = refw::field<void*>(info, kInfoAttackHit);
    for (uint8_t kind = 0; kind < wire_dd2::kBodyKinds; ++kind) {
        if (!attacker || attacker != g_ownHits[kind].load(std::memory_order_relaxed)) continue;
        Captured hit;
        hit.hit = hitController;
        hit.message.attackerKind = kind;
        hit.message.damage = refw::field<float>(info, kInfoDamage);
        hit.message.reaction = refw::field<float>(info, kInfoReaction);
        hit.message.reactionLevel = refw::field<int32_t>(info, kInfoReactionLevel);
        hit.message.damageType = refw::field<int32_t>(info, kInfoDamageType);
        std::lock_guard lock(g_capturedMutex);
        g_captured.push_back(hit);
        return;
    }
}

bool onState(std::span<const uint8_t> payload, uint64_t nowMs) {
    const auto states = wire_dd2::decodeStates(payload);
    if (!states) return false;
    for (const wire_dd2::EnemyState& state : *states) g_host[state.key] = {state, nowMs};
    return true;
}

bool onSpawn(std::span<const uint8_t> payload) {
    auto spawn = wire_dd2::decodeSpawn(payload);
    if (!spawn) return false;
    if (g_enabled) g_spawns.push_back(std::move(*spawn));
    return true;
}

}  // namespace enemy_mirror
