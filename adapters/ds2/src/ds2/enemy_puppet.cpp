// DEATH STRANDING 2: the guest's puppets of the host's enemies (docs/DS2_NOTES.md, "Enemy puppets"). The guest's own
// enemies are built by the engine as usual and tamed at once (`adopt`): their AI put to sleep (the byte the engine's own entity sleep sets), while the entity stays awake: it
// is drawn and animated, its camp and spawn setup keep their bookkeeping (cutting the components off from the entity
// messages instead broke the camp's cleanup when its tile unloaded and crashed the game, and putting the whole entity
// to sleep makes it invisible and stops its animation). The host's ENEMY_SPAWN names an enemy by its entity
// UUID, which is the same on both machines for the same spawnpoint; the puppet is the tamed enemy with that UUID, and
// from then on only the host's reports move it. Death and removal use the engine's own calls.
#include "ds2/enemy_puppet.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "decima/safe_read.h"
#include "ds2/enemy_pose.h"
#include "ds2/engine.h"
#include "ds2/enemy_host.h"
#include "ds2/enemy_vitals.h"
#include "ds2/entity_lookup.h"
#include "ds2/place.h"
#include "ds2/remote_animation.h"
#include "ds2/sim_tick.h"
#include "enemy_wire.h"
#include "game.h"
#include "log.h"

namespace {

constexpr uintptr_t kKill = 0x14013bef0;    // Entity::Kill(entity, attacker, params)
constexpr uintptr_t kRemove = 0x14014ba20;  // Entity::Remove(entity, immediate)
constexpr uintptr_t kAiComponentRecord = 0x1442A1160;  // AIIndividualComponent
constexpr uintptr_t kAiIndividual = 0x50, kAiActivity = 0x23;  // AIIndividual = component + 0x50; its activity byte
constexpr uint8_t kAiAsleep = 2;  // 0 none, 2 asleep, 3 awake: the AI manager does not decide for a sleeping individual
constexpr uintptr_t kEntityUuid = 0x10;
constexpr ULONGLONG kPruneMs = 500;
constexpr ULONGLONG kAsleepEveryMs = 250;  // the engine puts the AI awake only on a wake event
constexpr ULONGLONG kMaxExtrapolationMs = 300;  // no state this long: the puppet stands where it was
constexpr ULONGLONG kRegistrationMs = 5000;     // a built entity joins the engine's entity map a little later
constexpr ULONGLONG kReportMs = 5000;
constexpr ULONGLONG kAnnouncementsSettledMs = 3000;  // no new announcement this long: the host's snapshot is complete
constexpr ULONGLONG kUnmatchedMs = 20000;            // a tamed enemy the host never announced is removed after this long
constexpr size_t kMaxQueued = 1024;
constexpr float kMillisecondsPerSecond = 1000.0f;

using Uuid = std::array<uint8_t, enemy_wire::kUuidSize>;

struct UuidHash {
    size_t operator()(const Uuid& id) const {
        uint64_t half;
        std::memcpy(&half, id.data(), sizeof(half));
        return static_cast<size_t>(half);
    }
};

struct Tamed {
    uintptr_t entity;
    ULONGLONG at;
};

struct Puppet {
    uintptr_t entity = 0;
    Uuid uuid{};
    enemy_wire::EnemyState state{};
    ULONGLONG receivedAt = 0;
    ULONGLONG placedFor = 0;  // the report (by its receive time) the last placement used
    bool dead = false;
    uint8_t appliedHealth = enemy_wire::kHealthUnknown;
};

std::atomic<bool> g_adoptExisting{false};
std::mutex g_animationMutex;  // guards g_animation: the pose evaluation reads it on other threads
std::unordered_map<uintptr_t, remote_animation::VariableValues> g_animation;  // puppet entity -> the host's variables
std::mutex g_mutex;  // guards the queues and the tamed table (spawn workers and the net thread fill them)
std::vector<enemy_wire::EnemySpawn> g_spawns;
std::vector<enemy_wire::EnemyState> g_states;
std::vector<enemy_wire::EnemyGone> g_gone;
std::vector<enemy_wire::EnemyAnim> g_anims;
std::unordered_map<Uuid, Tamed, UuidHash> g_tamed;  // entity UUID -> an enemy of this world, tamed

// Simulation thread only.
std::unordered_map<uint16_t, enemy_wire::EnemySpawn> g_pending;  // announced, no tamed enemy of this world to be it yet
std::unordered_map<uint16_t, Puppet> g_puppets;
std::unordered_set<Uuid, UuidHash> g_announced;  // every entity UUID the host has announced
ULONGLONG g_lastAnnouncementAt = 0;

bool killGuarded(uintptr_t entity) {
    __try {
        reinterpret_cast<void (*)(uintptr_t, uintptr_t, uintptr_t)>(ds2::at(kKill))(entity, 0, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Puts the enemy's AI to sleep the way the engine's own entity sleep does, while the entity stays awake: it is drawn,
// animated and moved, and decides nothing. The engine sets the byte back to awake when it wakes the entity, so this is
// repeated every tick.
void sleepAi(uintptr_t entity) {
    const uintptr_t component = ds2::componentByRecord(entity, kAiComponentRecord);
    if (component) ds2::field<uint8_t>(component + kAiIndividual, kAiActivity) = kAiAsleep;
}

bool removeGuarded(uintptr_t entity) {
    __try {
        reinterpret_cast<void (*)(uintptr_t, bool)>(ds2::at(kRemove))(entity, true);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The tamed enemy this announcement is about, once the engine has it registered; 0 while it has not.
uintptr_t tamedFor(const enemy_wire::EnemySpawn& spawn) {
    Uuid id;
    std::copy(spawn.entityUuid, spawn.entityUuid + id.size(), id.begin());
    std::lock_guard lock(g_mutex);
    const auto it = g_tamed.find(id);
    return it != g_tamed.end() && ds2::entityExists(id.data()) ? it->second.entity : 0;
}

void bind(const enemy_wire::EnemySpawn& spawn, uintptr_t entity) {
    Puppet puppet;
    puppet.entity = entity;
    std::copy(spawn.entityUuid, spawn.entityUuid + puppet.uuid.size(), puppet.uuid.begin());
    puppet.state.netId = spawn.netId;
    puppet.state.pose = spawn.pose;
    puppet.state.healthRatio = enemy_wire::kHealthUnknown;
    puppet.receivedAt = GetTickCount64();
    g_puppets[spawn.netId] = puppet;
    logger::write("enemy_puppet: enemy %u is now driven by the host (%p)", spawn.netId, reinterpret_cast<void*>(entity));
}

void forgetAnimation(uintptr_t entity) {
    std::lock_guard lock(g_animationMutex);
    g_animation.erase(entity);
}

void handleGone(const enemy_wire::EnemyGone& gone) {
    g_pending.erase(gone.netId);
    const auto it = g_puppets.find(gone.netId);
    if (it == g_puppets.end()) return;
    Puppet& puppet = it->second;
    if (gone.reason == static_cast<uint8_t>(enemy_wire::GoneReason::Died)) {
        puppet.dead = true;
        if (ds2::entityExists(puppet.uuid.data())) killGuarded(puppet.entity);
        return;
    }
    if (ds2::entityExists(puppet.uuid.data())) removeGuarded(puppet.entity);
    forgetAnimation(puppet.entity);
    g_puppets.erase(it);
}

void handleState(const enemy_wire::EnemyState& state) {
    if (const auto it = g_puppets.find(state.netId); it != g_puppets.end()) {
        it->second.state = state;
        it->second.receivedAt = GetTickCount64();
    } else if (const auto waiting = g_pending.find(state.netId); waiting != g_pending.end()) {
        waiting->second.pose = state.pose;
    }
}

void handleAnimation(const enemy_wire::EnemyAnim& anim) {
    const auto it = g_puppets.find(anim.netId);
    if (it == g_puppets.end()) return;
    std::lock_guard lock(g_animationMutex);
    remote_animation::VariableValues& values = g_animation[it->second.entity];
    for (const remote_animation::Change& change : anim.changes) values.set(change);
}

void applyHealth(Puppet& puppet) {
    if (puppet.state.healthRatio == puppet.appliedHealth) return;
    enemy_vitals::applyHealth(puppet.entity, puppet.state.healthRatio);
    puppet.appliedHealth = puppet.state.healthRatio;
}

void place(Puppet& puppet, ULONGLONG now) {
    const bool standing = puppet.state.velocity[0] == 0 && puppet.state.velocity[1] == 0 && puppet.state.velocity[2] == 0;
    if (standing && puppet.placedFor == puppet.receivedAt) return;  // nothing moved since the last placement
    puppet.placedFor = puppet.receivedAt;
    const ULONGLONG age = std::min(now - puppet.receivedAt, kMaxExtrapolationMs);
    const float seconds = static_cast<float>(age) / kMillisecondsPerSecond;
    decima::WorldTransform transform = enemy_pose::fromWire(puppet.state.pose);
    transform.position.x += puppet.state.velocity[0] * seconds;
    transform.position.y += puppet.state.velocity[1] * seconds;
    transform.position.z += puppet.state.velocity[2] * seconds;
    ds2::placeEntity(puppet.entity, transform,
                     {puppet.state.velocity[0], puppet.state.velocity[1], puppet.state.velocity[2]});
}

// Keeps every tamed enemy's AI asleep.
void keepAiAsleep() {
    std::lock_guard lock(g_mutex);
    for (const auto& [id, tamed] : g_tamed) {
        if (ds2::entityExists(id.data())) sleepAi(tamed.entity);
    }
}

// Forgets tamed enemies the engine no longer has.
void pruneTamed(ULONGLONG now) {
    std::lock_guard lock(g_mutex);
    std::erase_if(g_tamed, [now](const auto& entry) {
        return now - entry.second.at > kRegistrationMs && !ds2::entityExists(entry.first.data());
    });
}

// Removes the tamed enemies the host's complete snapshot does not have: the host's world has no such enemy, so the guest
// must not show a frozen one.
void removeUnmatched(ULONGLONG now) {
    if (!g_lastAnnouncementAt || now - g_lastAnnouncementAt < kAnnouncementsSettledMs) return;
    std::lock_guard lock(g_mutex);
    int removed = 0;
    for (auto it = g_tamed.begin(); it != g_tamed.end();) {
        if (now - it->second.at < kUnmatchedMs || g_announced.contains(it->first)) {
            ++it;
            continue;
        }
        if (ds2::entityExists(it->first.data())) removeGuarded(it->second.entity);
        it = g_tamed.erase(it);
        ++removed;
    }
    if (removed) logger::write("enemy_puppet: removed %d enemies of this world that the host does not have", removed);
}

// Guest, simulation thread.
void tick() {
    static ULONGLONG lastPrune = 0, lastReport = 0, lastAsleep = 0;
    std::vector<enemy_wire::EnemySpawn> spawns;
    std::vector<enemy_wire::EnemyState> states;
    std::vector<enemy_wire::EnemyGone> gone;
    std::vector<enemy_wire::EnemyAnim> anims;
    {
        std::lock_guard lock(g_mutex);
        spawns.swap(g_spawns);
        states.swap(g_states);
        gone.swap(g_gone);
        anims.swap(g_anims);
    }
    if (g_adoptExisting.exchange(false)) {
        for (const uintptr_t entity : enemy_host::release()) enemy_puppet::adopt(entity);
    }
    if (const ULONGLONG at = GetTickCount64(); at - lastAsleep >= kAsleepEveryMs) {
        lastAsleep = at;
        keepAiAsleep();
    }
    for (const enemy_wire::EnemySpawn& spawn : spawns) {
        g_pending[spawn.netId] = spawn;
        Uuid id;
        std::copy(spawn.entityUuid, spawn.entityUuid + id.size(), id.begin());
        g_announced.insert(id);
        g_lastAnnouncementAt = GetTickCount64();
    }
    for (const enemy_wire::EnemyState& state : states) handleState(state);
    for (const enemy_wire::EnemyGone& event : gone) handleGone(event);
    for (const enemy_wire::EnemyAnim& anim : anims) handleAnimation(anim);
    for (auto it = g_pending.begin(); it != g_pending.end();) {
        const uintptr_t entity = tamedFor(it->second);
        if (entity) bind(it->second, entity);
        it = entity ? g_pending.erase(it) : std::next(it);
    }
    const ULONGLONG now = GetTickCount64();
    if ((!g_pending.empty() || !g_puppets.empty()) && now - lastReport >= kReportMs) {
        lastReport = now;
        logger::write("enemy_puppet: %zu enemies driven by the host, %zu announced and not found here", g_puppets.size(),
                      g_pending.size());
    }
    if (now - lastPrune >= kPruneMs) {
        lastPrune = now;
        pruneTamed(now);
        removeUnmatched(now);
    }
    for (auto it = g_puppets.begin(); it != g_puppets.end();) {
        Puppet& puppet = it->second;
        if (!ds2::entityExists(puppet.uuid.data())) {  // every tick: the engine frees entities of a world it unloads
            logger::write("enemy_puppet: enemy %u is gone from this world", it->first);
            forgetAnimation(puppet.entity);
            it = g_puppets.erase(it);
            continue;
        }
        if (!puppet.dead) {
            place(puppet, now);
            applyHealth(puppet);
        }
        ++it;
    }
}

}  // namespace

namespace enemy_puppet {

void installEarly() { sim_tick::add(&tick, "enemy puppets", sim_tick::Gate::Gameplay); }

bool animate(uintptr_t manager, uintptr_t owner) {
    std::lock_guard lock(g_animationMutex);
    const auto it = g_animation.find(owner);
    if (it == g_animation.end()) return false;
    remote_animation::applyValues(manager, it->second);
    return true;
}

void adoptExisting() { g_adoptExisting = true; }

void adopt(uintptr_t entity) {
    sleepAi(entity);
    Uuid id{};
    decima::safeCopy(id.data(), entity + kEntityUuid, id.size());
    std::lock_guard lock(g_mutex);
    g_tamed[id] = {entity, GetTickCount64()};
}

}  // namespace enemy_puppet

namespace game {

void puppetSpawn(const enemy_wire::EnemySpawn& spawn) {
    std::lock_guard lock(g_mutex);
    if (g_spawns.size() < kMaxQueued) g_spawns.push_back(spawn);
}

void puppetStates(const std::vector<enemy_wire::EnemyState>& states) {
    std::lock_guard lock(g_mutex);
    if (g_states.size() + states.size() <= kMaxQueued) g_states.insert(g_states.end(), states.begin(), states.end());
}

void puppetAnim(enemy_wire::EnemyAnim anim) {
    std::lock_guard lock(g_mutex);
    if (g_anims.size() < kMaxQueued) g_anims.push_back(std::move(anim));
}

void puppetGone(const enemy_wire::EnemyGone& gone) {
    std::lock_guard lock(g_mutex);
    if (g_gone.size() < kMaxQueued) g_gone.push_back(gone);
}

}  // namespace game
