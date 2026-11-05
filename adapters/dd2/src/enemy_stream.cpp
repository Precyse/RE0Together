#include "enemy_stream.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <vector>

#include "calls.h"
#include "enemy_rule.h"
#include "enemy_spawn.h"
#include "local_body.h"
#include "log.h"
#include "peer_bodies.h"
#include "peer_puppet.h"
#include "refw.h"
#include "roster.h"
#include "session.h"
#include "wire_dd2.h"
#include "world_gate.h"

namespace {

constexpr uint64_t kStatePeriodMs = 100;   // 10 Hz
constexpr uint64_t kSpawnPeriodMs = 250;   // 4 records per second
constexpr uint64_t kSummaryPeriodMs = 30000;
constexpr bool kNotFromScript = false;     // directDamage's Boolean
// DamageInfo (static_checks.md P7-5): what a replayed hit carries.
constexpr uint32_t kInfoDamage = 0x1A0;
constexpr uint32_t kInfoReaction = 0x128;
constexpr uint32_t kInfoReactionLevel = 0x1B0;
constexpr uint32_t kInfoDamageType = 0x18C;
constexpr const char* kDamageInfoType = "app.HitController.DamageInfo";

bool g_enabled = false;
uint64_t g_lastStateMs = 0, g_lastSpawnMs = 0, g_lastSummaryMs = 0;
std::deque<roster_rule::Key> g_wanted;
enemy_rule::HitBudget g_budget;
uint32_t g_hitsApplied = 0, g_hitsRefused = 0, g_spawnsSent = 0;

bool active() {
    return g_enabled && session::isHost() && world_gate::place() == world_gate_rule::Place::InGame;
}

std::optional<roster::Member> memberWith(const roster_rule::Key& key) {
    for (const roster::Member& member : roster::members()) {
        if (member.key == key) return member;
    }
    return std::nullopt;
}

std::vector<enemy_rule::Point> guestPositions() {
    std::vector<enemy_rule::Point> out;
    for (uint8_t slot : session::matchingPeers()) {
        if (const auto body = peer_bodies::latest(slot, wire_dd2::kKindArisen)) {
            out.push_back({body->position[0], body->position[1], body->position[2]});
        }
    }
    return out;
}

void sendStates() {
    const std::vector<enemy_rule::Point> guests = guestPositions();
    if (guests.empty()) return;
    std::vector<wire_dd2::EnemyState> states;
    for (const roster::Member& member : roster::members()) {
        const auto sample = local_body::sampleCharacter(member.character);
        if (!sample || !enemy_rule::streamed({sample->position[0], sample->position[1], sample->position[2]}, guests)) continue;
        wire_dd2::EnemyState state{};
        state.key = member.key;
        std::copy(std::begin(sample->position), std::end(sample->position), state.position);
        state.yaw = sample->yaw;
        state.hp = sample->hp;
        std::copy(std::begin(sample->actionHash), std::end(sample->actionHash), state.actionHash);
        state.speedType = sample->speedType;
        state.flags = sample->dead ? wire_dd2::kEnemyDead : 0;
        states.push_back(state);
    }
    for (size_t first = 0; first < states.size(); first += wire_dd2::kMaxStatesPerFrame) {
        const size_t count = std::min(states.size() - first, size_t{wire_dd2::kMaxStatesPerFrame});
        session::send(wire_dd2::kEnemyState, false, wire_dd2::encodeStates(world_gate::loadSerial(), {states.data() + first, count}));
    }
}

void sendSpawn() {
    while (!g_wanted.empty()) {
        const roster_rule::Key key = g_wanted.front();
        g_wanted.pop_front();
        const auto member = memberWith(key);
        if (!member) continue;  // gone on our side too
        const auto json = enemy_spawn::recordOf(member->character);
        if (json && json->size() <= wire_dd2::kMaxSpawnJson &&
            session::send(wire_dd2::kEnemySpawn, true, wire_dd2::encodeSpawn(key, *json))) {
            ++g_spawnsSent;
        }
        return;
    }
}

// The guest's hit on our enemy, as the engine's own direct damage from the guest's puppet.
bool apply(uint8_t slot, const wire_dd2::EnemyHit& hit) {
    const auto member = memberWith(hit.key);
    void* target = member ? calls::objectResult(calls::kGetHit, member->character) : nullptr;
    void* hits = refw::managedSingleton("app.HitManager");
    const refw::InvokeResult* maxHp = member ? refw::invoke(calls::method(calls::kGetOriginalMaxHp), member->character) : nullptr;
    if (!target || !hits || !maxHp || !enemy_rule::damageOk(hit.damage, maxHp->f)) return false;
    void* info = refw::createInstance(kDamageInfoType);
    if (!info) return false;
    refw::field<float>(info, kInfoDamage) = hit.damage;
    refw::field<float>(info, kInfoReaction) = hit.reaction;
    refw::field<int32_t>(info, kInfoReactionLevel) = hit.reactionLevel;
    refw::field<int32_t>(info, kInfoDamageType) = hit.damageType;
    const auto puppet = peer_puppet::body(slot, hit.attackerKind);
    void* attacker = puppet ? puppet->hit : nullptr;
    void* attackerObject = puppet ? calls::objectResult(calls::kGetGameObject, puppet->character) : nullptr;
    const bool queued = refw::invoke(calls::method(calls::kDirectDamage), hits,
                                     {info, target, attackerObject, reinterpret_cast<void*>(uintptr_t{kNotFromScript}), attacker}) !=
                        nullptr;
    refw::release(info);  // the damage list holds its own reference
    return queued;
}

}  // namespace

namespace enemy_stream {

void setEnabled(bool on) { g_enabled = on; }

void tick(uint64_t nowMs) {
    if (nowMs - g_lastSummaryMs >= kSummaryPeriodMs) {
        g_lastSummaryMs = nowMs;
        if (g_hitsApplied || g_hitsRefused || g_spawnsSent) {
            logger::write("enemy_stream: %u guest hits replayed, %u refused, %u records sent in 30 s", g_hitsApplied, g_hitsRefused,
                          g_spawnsSent);
        }
        g_hitsApplied = g_hitsRefused = g_spawnsSent = 0;
    }
    if (!active()) {
        g_wanted.clear();
        return;
    }
    if (nowMs - g_lastStateMs >= kStatePeriodMs) {
        g_lastStateMs = nowMs;
        sendStates();
    }
    if (nowMs - g_lastSpawnMs >= kSpawnPeriodMs) {
        g_lastSpawnMs = nowMs;
        sendSpawn();
    }
}

bool onHit(uint8_t slot, std::span<const uint8_t> payload, uint64_t nowMs) {
    const auto hit = wire_dd2::decodeEnemyHit(payload);
    if (!hit) return false;
    if (active() && g_budget.admit(slot, hit->key, nowMs) && apply(slot, *hit)) {
        ++g_hitsApplied;
    } else {
        ++g_hitsRefused;
    }
    return true;
}

bool onWant(uint8_t, std::span<const uint8_t> payload) {
    const auto want = wire_dd2::decodeRoster(payload);
    if (!want) return false;
    if (!active()) return true;
    for (const roster_rule::Key& key : want->keys) {
        if (std::find(g_wanted.begin(), g_wanted.end(), key) == g_wanted.end()) g_wanted.push_back(key);
    }
    return true;
}

}  // namespace enemy_stream
