// DEATH STRANDING 2: where the enemies come from. Every EntitySpawnInfo spawn (spawnpoints, collective and multi
// spawnpoints, AI groups, encounters, the catcher territory, scripted CreateEntity) builds its entity in
// EntitySpawnInfo::CreateEntity, reached from GetOrCreateEntity. This hook sees every enemy the engine builds: on the
// host it reports the enemy to ds2/enemy_host; on a guest the enemy is built as usual (its camp, its spawn setup and its
// owner all expect it) and then tamed, put to sleep so the host alone decides what it does (ds2/enemy_puppet; docs/DS2_NOTES.md, "Enemy puppets").
#include "ds2/enemy_spawn.h"

#include <array>
#include <atomic>

#include "decima/safe_read.h"
#include "ds2/enemy_host.h"
#include "ds2/enemy_puppet.h"
#include "ds2/engine.h"
#include "game.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kCreateEntity = 0x14016d350;           // EntitySpawnInfo::CreateEntity(info) -> Entity* or 0
constexpr uintptr_t kFindComponentResource = 0x14016a040;  // (EntityResource*, type record) -> component resource

// EntitySpawnInfo.
constexpr uintptr_t kInfoResourceRef = 0x198;  // streaming reference: [ref] = holder, [ref + 8] flags
constexpr uintptr_t kRefFlags = 0x8;
constexpr int kRefStateShift = 52;           // (flags >> 52) is the reference's state byte
constexpr uint64_t kRefLoadedBit = 1 << 7;   // set in the state byte: the resource is loaded
constexpr uintptr_t kHolderResource = 0x20;  // holder + 0x20 = the EntityResource
constexpr uintptr_t kResourceUuid = 0x10;
constexpr size_t kUuidSize = 16;
constexpr int kTamedLogEvery = 100;

// Component resource types that make an entity resource an enemy, and the one that excludes it (porters are built on
// the bandit type).
constexpr uintptr_t kEnemyComponentRecords[] = {
    0x144474f90,  // DSSimpleSneakingNpcComponentResource: armed humans, MULEs, ghost mechs
    0x14444ce70,  // DSGazerComponentResource: BTs
    0x1444531f0,  // DSCatcherComponentResource
    0x144460330,  // DSHunterComponentResource
    0x144469640,  // DSMechOctopusComponentResource
    0x1444397c0,  // DSRedSamuraiComponentResource
    0x14445a160,  // DSBTMembershipComponentResource
};
constexpr uintptr_t kPorterRecord = 0x144473870;  // DSPorterComponentResource

using CreateFn = uintptr_t (*)(uintptr_t info);
using FindFn = uintptr_t (*)(uintptr_t resource, uintptr_t record);

CreateFn g_create = nullptr;
std::atomic<bool> g_tame{false};
std::atomic<int> g_tamed{0};

uintptr_t resourceOf(uintptr_t info) {
    const uintptr_t ref = decima::readPointer(info + kInfoResourceRef);
    uint64_t flags = 0;
    if (!ref || !decima::safeRead(ref + kRefFlags, flags) || ((flags >> kRefStateShift) & kRefLoadedBit) == 0) return 0;
    return decima::readPointer(decima::readPointer(ref) + kHolderResource);
}

bool isEnemyResource(uintptr_t resource) {
    if (!resource) return false;
    const auto find = reinterpret_cast<FindFn>(ds2::at(kFindComponentResource));
    if (find(resource, ds2::at(kPorterRecord))) return false;
    for (const uintptr_t record : kEnemyComponentRecords) {
        if (find(resource, ds2::at(record))) return true;
    }
    return false;
}

// Runs on the spawn workers as well as the simulation thread.
uintptr_t createDetour(uintptr_t info) {
    const uintptr_t resource = resourceOf(info);
    const bool enemy = isEnemyResource(resource);
    const uintptr_t entity = g_create(info);
    if (!enemy || !entity) return entity;
    if (g_tame.load()) {
        if (const int count = ++g_tamed; count == 1 || count % kTamedLogEvery == 0) {
            logger::write("enemy_spawn: tamed %d enemies", count);
        }
        enemy_puppet::adopt(entity);
    } else {
        std::array<uint8_t, kUuidSize> resourceUuid{};
        decima::safeCopy(resourceUuid.data(), resource + kResourceUuid, resourceUuid.size());
        enemy_host::add(entity, resourceUuid);
    }
    return entity;
}

}  // namespace

namespace enemy_spawn {

void installEarly() {
    hooks::install("enemy spawn", ds2::at(kCreateEntity), reinterpret_cast<void*>(&createDetour),
                   reinterpret_cast<void**>(&g_create));
}

}  // namespace enemy_spawn

namespace game {

void tameEnemies(bool tame) {
    if (g_tame.exchange(tame) == tame) return;
    logger::write("enemy_spawn: enemies %s", tame ? "tamed" : "left alone");
    if (tame) enemy_puppet::adoptExisting();
}

}  // namespace game
