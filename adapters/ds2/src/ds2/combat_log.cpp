// DEATH STRANDING 2: the damage log (see ds2/combat_log.h).
#include "ds2/combat_log.h"

#include <cstdio>
#include <mutex>
#include <set>
#include <tuple>

#include "decima/safe_read.h"
#include "ds2/damage_params.h"
#include "ds2/enemy_vitals.h"
#include "ds2/entity_lookup.h"
#include "ds2/remote_player.h"
#include "enemy_directory.h"
#include "log.h"

namespace {

constexpr uintptr_t kParamsType = 0x00, kParamsFlags = 0x08, kParamsPart = 0x68;
constexpr size_t kNameSize = 40;
constexpr size_t kMaxRemembered = 512;  // attack ids kept to log each hit once
constexpr float kMinLoggedAmount = 0.5f;  // contact and physics touches (fractions of a point, one every few ms) are not logged...

// "Sam", "the remote body", "enemy N" (a directory enemy) or "other entity 0x..".
void nameOf(uintptr_t entity, char (&out)[kNameSize]) {
    enemy_directory::Uuid uuid;
    if (!entity) {
        std::snprintf(out, sizeof(out), "nobody");
    } else if (entity == remote_player::samEntity()) {
        std::snprintf(out, sizeof(out), "Sam");
    } else if (entity == remote_player::entity()) {
        std::snprintf(out, sizeof(out), "the remote body");
    } else if (ds2::entityUuid(entity, uuid) && enemy_directory::netIdOf(uuid) != 0) {
        std::snprintf(out, sizeof(out), "enemy %u", enemy_directory::netIdOf(uuid));
    } else {
        std::snprintf(out, sizeof(out), "other entity %p", reinterpret_cast<void*>(entity));
    }
}

std::mutex g_seenMutex;
std::set<std::tuple<uint64_t, uintptr_t, char>> g_seen;  // (attack id, victim, first letter of the outcome) already logged

// True the first time a hit with this attack id is logged for this victim and kind of outcome: a bullet's hit repeats
// every few milliseconds while it overlaps the victim.
bool firstTime(uint64_t attackId, uintptr_t victim, char outcome) {
    if (!attackId) return true;
    std::lock_guard lock(g_seenMutex);
    if (g_seen.size() >= kMaxRemembered) g_seen.clear();
    return g_seen.insert({attackId, victim, outcome}).second;
}

bool trackedVictim(uintptr_t victim) {
    enemy_directory::Uuid uuid;
    return victim == remote_player::samEntity() || victim == remote_player::entity() ||
           (ds2::entityUuid(victim, uuid) && enemy_directory::netIdOf(uuid) != 0);
}

}  // namespace

namespace combat_log {

bool isTracked(uintptr_t victim) { return trackedVictim(victim); }

Snapshot before(uintptr_t victim, uintptr_t params) {
    Snapshot snapshot;
    if (!params || !victim || !trackedVictim(victim)) return snapshot;
    snapshot.tracked = true;
    snapshot.healthBefore = enemy_vitals::readHealth(victim);
    snapshot.attacker = ds2::damage::attackerOf(params);
    return snapshot;
}

void after(const Snapshot& snapshot, uintptr_t victim, uintptr_t params, const char* outcome) {
    if (!snapshot.tracked) return;
    char victimName[kNameSize], attackerName[kNameSize];
    nameOf(victim, victimName);
    nameOf(snapshot.attacker, attackerName);
    uint64_t type = 0;
    uint32_t flags = 0;
    int32_t part = 0;
    float amount = 0;
    decima::safeRead(params + kParamsType, type);
    decima::safeRead(params + kParamsFlags, flags);
    decima::safeRead(params + kParamsPart, part);
    amount = ds2::damage::amountOf(params);
    const uint8_t healthAfter = enemy_vitals::readHealth(victim);
    // ...unless they changed the victim's health or a player dealt them: a real weapon hit carries no amount in the
    // parameters (the engine works it out while applying).
    const bool byPlayer = snapshot.attacker == remote_player::samEntity() || snapshot.attacker == remote_player::entity();
    if (amount < kMinLoggedAmount && healthAfter == snapshot.healthBefore && !byPlayer) return;
    const uintptr_t data = ds2::damage::contextOf(params) + ds2::damage::kContextData;
    uint64_t attackId = 0, source = 0;
    uintptr_t resource = 0;
    uint16_t attackType = 0;
    if (data != ds2::damage::kContextData) {
        decima::safeRead(data + ds2::damage::kDataId, attackId);
        decima::safeRead(data + ds2::damage::kDataType, attackType);
        decima::safeRead(data + ds2::damage::kDataSource, source);
        decima::safeRead(data + ds2::damage::kDataResource, resource);
    }
    if (!firstTime(attackId, victim, outcome[0])) return;
    logger::write("combat_hit: %s hit %s (%s): damage type %llx, flags %x, part %d, amount %.1f, attack id %llx type %x resource %p source %llx, health %u -> %u",
                  attackerName, victimName, outcome, static_cast<unsigned long long>(type), flags, part, amount,
                  static_cast<unsigned long long>(attackId), attackType, reinterpret_cast<void*>(resource),
                  static_cast<unsigned long long>(source),
                  snapshot.healthBefore, healthAfter);
}

}  // namespace combat_log
