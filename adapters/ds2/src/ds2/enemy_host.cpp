// DEATH STRANDING 2: the host's enemies, reported to the guests (docs/DS2_NOTES.md, "Enemy puppets"). The spawn hook
// (enemy_spawn) adds every enemy entity the engine builds; the simulation tick announces each one once (ENEMY_SPAWN),
// samples its pose every 100 ms (ENEMY_STATE), and reports its death and its disappearance (ENEMY_GONE). Entities are
// held as raw pointers and checked by UUID against the engine's entity map before every read.
#include "ds2/enemy_host.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/enemy_vitals.h"
#include "ds2/entity_lookup.h"
#include "ds2/entity_wake.h"
#include "enemy_directory.h"
#include "ds2/place.h"
#include "ds2/remote_animation.h"
#include "ds2/remote_player.h"
#include "ds2/enemy_pose.h"
#include "ds2/sim_tick.h"
#include "enemy_wire.h"
#include "game.h"
#include "log.h"

namespace {

constexpr uintptr_t kEntityUuid = 0x10;
constexpr uintptr_t kEntityFlags = 0x98;
constexpr ULONGLONG kSampleMs = 100;
constexpr size_t kMaxTracked = 512;
// The host's streaming sleeps enemies far from the HOST's player; an enemy within this range of the partner's body is kept awake
// (re-asked at most this often per enemy), so it can fight the partner and take its hits.
constexpr double kKeepAwakeMetres = 80.0;
constexpr ULONGLONG kWakeRetryMs = 1000;
constexpr size_t kMaxQueued = 1024;
constexpr float kMillisecondsPerSecond = 1000.0f;
constexpr double kMinMoveMeters = 0.03;  // an enemy that moved less than this and did not turn is not reported again...
constexpr float kMinTurn = 0.01f;
constexpr double kAnimationRadius = 50.0;  // metres around the partner: further enemies are not animated on its side
constexpr ULONGLONG kAnimationSnapshotMs = 10000;  // a new enemy in range and a requested snapshot send one at once
constexpr ULONGLONG kKeepaliveMs = 2000;  // ...for this long (a camp holds hundreds of enemies that stand still)

struct Tracked {
    uint16_t netId;
    uintptr_t entity;
    std::array<uint8_t, enemy_wire::kUuidSize> entityUuid;
    std::array<uint8_t, enemy_wire::kUuidSize> resourceUuid;
    bool announced = false;
    bool deadReported = false;
    bool havePrevious = false;
    decima::WorldPosition previous{};
    ULONGLONG previousAt = 0;
    enemy_wire::Pose sentPose{};
    ULONGLONG sentAt = 0;
    std::unique_ptr<remote_animation::VariableValues> sentVariables;  // only while the partner is near
    ULONGLONG variablesSnapshotAt = 0;
    uint8_t sentHealth = enemy_wire::kHealthUnknown;
    ULONGLONG wakeAskedAt = 0;
};

std::mutex g_mutex;  // guards everything below (spawn workers add, the simulation thread samples, the net thread takes)
std::vector<Tracked> g_tracked;
uint16_t g_nextNetId = 1;
std::vector<enemy_wire::EnemySpawn> g_spawns;
std::vector<enemy_wire::EnemyState> g_states;
std::vector<enemy_wire::EnemyGone> g_gone;
std::vector<enemy_wire::EnemyAnim> g_anims;
bool g_sharing = false;
bool g_snapshotRequested = false;

void pushGone(uint16_t netId, enemy_wire::GoneReason reason) {
    enemy_directory::forget(netId);
    if (g_gone.size() < kMaxQueued) g_gone.push_back({netId, static_cast<uint8_t>(reason), 0});
}

// The partner's body, where the enemies it should see animated are looked for; 0 when it does not exist yet.
bool partnerPosition(decima::WorldPosition& out) {
    decima::WorldTransform transform;
    if (!ds2::entityTransform(remote_player::entity(), transform)) return false;
    out = transform.position;
    return true;
}

// The enemy's animation variables that changed since the last report, while the partner is within sight of it.
void sampleAnimation(Tracked& enemy, const decima::WorldTransform& transform, const decima::WorldPosition* partner,
                     ULONGLONG now) {
    const bool inSight = partner && std::hypot(transform.position.x - partner->x, transform.position.y - partner->y) <= kAnimationRadius;
    const uintptr_t manager = inSight ? remote_animation::managerOf(enemy.entity) : 0;
    if (!manager) {
        enemy.sentVariables.reset();
        return;
    }
    if (!enemy.sentVariables) enemy.sentVariables = std::make_unique<remote_animation::VariableValues>();
    const bool snapshot = now - enemy.variablesSnapshotAt >= kAnimationSnapshotMs;
    if (snapshot) enemy.variablesSnapshotAt = now;
    std::vector<remote_animation::Change> changes = remote_animation::sampleChanges(manager, *enemy.sentVariables, snapshot);
    if (changes.empty() || g_anims.size() >= kMaxQueued) return;
    enemy_wire::EnemyAnim anim;
    anim.netId = enemy.netId;
    anim.snapshot = snapshot;
    anim.changes = std::move(changes);
    g_anims.push_back(std::move(anim));
}

// Keeps an enemy near the partner's body awake: the engine sleeps it by the distance to the host's own player only.
void keepAwake(Tracked& enemy, const decima::WorldTransform& transform, const decima::WorldPosition& partner, ULONGLONG now) {
    const double dx = transform.position.x - partner.x, dy = transform.position.y - partner.y, dz = transform.position.z - partner.z;
    if (dx * dx + dy * dy + dz * dz > kKeepAwakeMetres * kKeepAwakeMetres || now - enemy.wakeAskedAt < kWakeRetryMs ||
        !ds2::entityAsleep(enemy.entity)) {
        return;
    }
    enemy.wakeAskedAt = now;
    ds2::wakeEntity(enemy.entity);
}

bool worthSending(const Tracked& enemy, const enemy_wire::EnemyState& state, ULONGLONG now) {
    const enemy_wire::Pose& pose = state.pose;
    if (now - enemy.sentAt >= kKeepaliveMs || state.healthRatio != enemy.sentHealth) return true;
    const double dx = pose.position[0] - enemy.sentPose.position[0];
    const double dy = pose.position[1] - enemy.sentPose.position[1];
    const double dz = pose.position[2] - enemy.sentPose.position[2];
    const float turn = std::abs(pose.rotation[3] - enemy.sentPose.rotation[3]) + std::abs(pose.rotation[4] - enemy.sentPose.rotation[4]);
    return dx * dx + dy * dy + dz * dz > kMinMoveMeters * kMinMoveMeters || turn > kMinTurn;
}

enemy_wire::EnemyState sample(Tracked& enemy, const decima::WorldTransform& transform, bool dead, ULONGLONG now) {
    enemy_wire::EnemyState state{};
    state.netId = enemy.netId;
    state.healthRatio = enemy_vitals::readHealth(enemy.entity);
    state.flags = dead ? enemy_wire::kStateDead : 0;
    state.pose = enemy_pose::toWire(transform);
    if (enemy.havePrevious && now > enemy.previousAt) {
        const float seconds = static_cast<float>(now - enemy.previousAt) / kMillisecondsPerSecond;
        state.velocity[0] = static_cast<float>(transform.position.x - enemy.previous.x) / seconds;
        state.velocity[1] = static_cast<float>(transform.position.y - enemy.previous.y) / seconds;
        state.velocity[2] = static_cast<float>(transform.position.z - enemy.previous.z) / seconds;
    }
    enemy.previous = transform.position;
    enemy.previousAt = now;
    enemy.havePrevious = true;
    return state;
}

// Host, simulation thread.
void tick() {
    static ULONGLONG lastSample = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - lastSample < kSampleMs) return;
    lastSample = now;
    std::lock_guard lock(g_mutex);
    if (!g_sharing) return;
    if (g_snapshotRequested) {
        g_snapshotRequested = false;
        for (Tracked& enemy : g_tracked) {
            enemy.announced = false;
            enemy.variablesSnapshotAt = 0;
        }
    }
    decima::WorldPosition partnerAt{};
    const bool havePartner = partnerPosition(partnerAt);
    for (auto it = g_tracked.begin(); it != g_tracked.end();) {
        Tracked& enemy = *it;
        if (!ds2::entityExists(enemy.entityUuid.data())) {
            if (enemy.announced) pushGone(enemy.netId, enemy_wire::GoneReason::Despawned);
            it = g_tracked.erase(it);
            continue;
        }
        decima::WorldTransform transform;
        uint64_t flags = 0;
        if (ds2::entityTransform(enemy.entity, transform) && decima::safeRead(enemy.entity + kEntityFlags, flags)) {
            const bool dead = enemy_vitals::isDead(enemy.entity);
            if (havePartner && !dead) keepAwake(enemy, transform, partnerAt, now);
            if (!enemy.announced && g_spawns.size() < kMaxQueued) {
                enemy_wire::EnemySpawn spawn{};
                spawn.netId = enemy.netId;
                std::copy(enemy.entityUuid.begin(), enemy.entityUuid.end(), spawn.entityUuid);
                std::copy(enemy.resourceUuid.begin(), enemy.resourceUuid.end(), spawn.resourceUuid);
                spawn.pose = enemy_pose::toWire(transform);
                g_spawns.push_back(spawn);
                enemy.announced = true;
                enemy_directory::set(enemy.netId, enemy.entityUuid);
            }
            if (enemy.announced && !dead) sampleAnimation(enemy, transform, havePartner ? &partnerAt : nullptr, now);
            if (enemy.announced && dead && !enemy.deadReported) {
                logger::write("enemy_host: enemy %u (entity %p) died, ENEMY_GONE(Died) queued", enemy.netId,
                              reinterpret_cast<void*>(enemy.entity));
                pushGone(enemy.netId, enemy_wire::GoneReason::Died);
                enemy.deadReported = true;
            }
            if (enemy.announced && !dead && g_states.size() < kMaxQueued) {
                const enemy_wire::EnemyState state = sample(enemy, transform, false, now);
                if (worthSending(enemy, state, now)) {
                    enemy.sentPose = state.pose;
                    enemy.sentHealth = state.healthRatio;
                    enemy.sentAt = now;
                    g_states.push_back(state);
                }
            }
        }
        ++it;
    }
}

}  // namespace

namespace enemy_host {

void add(uintptr_t entity, const std::array<uint8_t, enemy_wire::kUuidSize>& resourceUuid) {
    Tracked enemy{};
    enemy.entity = entity;
    enemy.resourceUuid = resourceUuid;
    decima::safeCopy(enemy.entityUuid.data(), entity + kEntityUuid, enemy.entityUuid.size());
    std::lock_guard lock(g_mutex);
    if (g_tracked.size() >= kMaxTracked) return;
    enemy.netId = g_nextNetId++;
    g_tracked.push_back(std::move(enemy));
}

std::vector<uintptr_t> release() {
    std::lock_guard lock(g_mutex);
    std::vector<uintptr_t> entities;
    for (const Tracked& enemy : g_tracked) {
        if (ds2::entityExists(enemy.entityUuid.data())) entities.push_back(enemy.entity);
    }
    g_tracked.clear();
    return entities;
}

}  // namespace enemy_host

namespace game {

void shareEnemies(bool host) {
    std::lock_guard lock(g_mutex);
    if (g_sharing == host) return;
    g_sharing = host;
    if (!host) {
        g_spawns.clear();
        g_states.clear();
        g_gone.clear();
        g_anims.clear();
    } else {
        g_snapshotRequested = true;
    }
    logger::write("enemy_host: %s", host ? "reporting the enemies" : "not reporting");
}

void requestEnemySnapshot() {
    std::lock_guard lock(g_mutex);
    g_snapshotRequested = true;
}

std::vector<enemy_wire::EnemySpawn> takeEnemySpawns() {
    std::lock_guard lock(g_mutex);
    std::vector<enemy_wire::EnemySpawn> out;
    out.swap(g_spawns);
    return out;
}

std::vector<enemy_wire::EnemyState> takeEnemyStates() {
    std::lock_guard lock(g_mutex);
    std::vector<enemy_wire::EnemyState> out;
    out.swap(g_states);
    return out;
}

std::vector<enemy_wire::EnemyAnim> takeEnemyAnims() {
    std::lock_guard lock(g_mutex);
    std::vector<enemy_wire::EnemyAnim> out;
    out.swap(g_anims);
    return out;
}

std::vector<enemy_wire::EnemyGone> takeEnemyGone() {
    std::lock_guard lock(g_mutex);
    std::vector<enemy_wire::EnemyGone> out;
    out.swap(g_gone);
    return out;
}

}  // namespace game

namespace enemy_host {

void installEarly() { sim_tick::add(&tick, "enemy host", sim_tick::Gate::Gameplay); }

}  // namespace enemy_host
