// DEATH STRANDING 2: the damage log (see ds2/combat_log.h).
#include "ds2/combat_log.h"

#include <cstdio>

#include "decima/safe_read.h"
#include "ds2/enemy_vitals.h"
#include "ds2/entity_lookup.h"
#include "ds2/remote_player.h"
#include "enemy_directory.h"
#include "log.h"

namespace {

constexpr uintptr_t kParamsType = 0x00, kParamsFlags = 0x08, kParamsAttacker = 0x20, kParamsPart = 0x68, kParamsAmount = 0x78;
constexpr uintptr_t kEntityWeakTarget = 0x20;  // a weak pointer to an entity points at entity + 0x20
constexpr size_t kNameSize = 40;

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

bool isTracked(uintptr_t victim) {
    enemy_directory::Uuid uuid;
    return victim == remote_player::samEntity() || victim == remote_player::entity() ||
           (ds2::entityUuid(victim, uuid) && enemy_directory::netIdOf(uuid) != 0);
}

}  // namespace

namespace combat_log {

Snapshot before(uintptr_t victim, uintptr_t params) {
    Snapshot snapshot;
    if (!params || !victim || !isTracked(victim)) return snapshot;
    snapshot.tracked = true;
    snapshot.healthBefore = enemy_vitals::readHealth(victim);
    const uintptr_t target = decima::readPointer(params + kParamsAttacker);
    snapshot.attacker = target ? target - kEntityWeakTarget : 0;
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
    decima::safeRead(params + kParamsAmount, amount);
    logger::write("combat_hit: %s hit %s (%s): damage type %llx, flags %x, part %d, amount %.1f, health %u -> %u",
                  attackerName, victimName, outcome, static_cast<unsigned long long>(type), flags, part, amount,
                  snapshot.healthBefore, enemy_vitals::readHealth(victim));
}

}  // namespace combat_log
