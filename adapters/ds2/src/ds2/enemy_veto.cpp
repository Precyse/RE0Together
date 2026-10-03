// DEATH STRANDING 2: the guest's enemies come from the host, so the guest's own spawns of them are vetoed. Every
// EntitySpawnInfo spawn (spawnpoints, collective and multi spawnpoints, AI groups, encounters, the catcher territory,
// scripted CreateEntity) builds its entity in EntitySpawnInfo::CreateEntity, reached from GetOrCreateEntity. Returning 0
// there is the engine's own failure path (the info is marked failed and done, the owner's callback runs without an
// entity), so spawn groups complete and nothing waits on them (docs/DS2_NOTES.md, "Enemies").
#include "ds2/enemy_veto.h"

#include <atomic>
#include <cstdio>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "game.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kCreateEntity = 0x14016d350;           // EntitySpawnInfo::CreateEntity(info) -> Entity* or 0
constexpr uintptr_t kFindComponentResource = 0x14016a040;  // (EntityResource*, type record) -> component resource

// EntitySpawnInfo.
constexpr uintptr_t kInfoEntityUuid = 0x40;
constexpr uintptr_t kInfoResourceRef = 0x198;  // streaming reference: [ref] = holder, [ref + 8] flags
constexpr uintptr_t kRefFlags = 0x8;
constexpr int kRefLoadedShift = 52;        // the top bit of (flags >> 52): the resource is loaded
constexpr uintptr_t kHolderResource = 0x20;  // holder + 0x20 = the EntityResource
constexpr size_t kUuidSize = 16;
constexpr int kMaxLogged = 60;
constexpr int kPassedLogEvery = 25;

// Component resource types that make an entity resource an enemy, and the one that excludes it (porters are built on
// the bandit type).
struct EnemyKind {
    uintptr_t record;
    const char* name;
};
constexpr EnemyKind kEnemyKinds[] = {
    {0x144474f90, "armed human, MULE, ghost mech"},  // DSSimpleSneakingNpcComponentResource
    {0x14444ce70, "BT"},                             // DSGazerComponentResource
    {0x1444531f0, "catcher"},                        // DSCatcherComponentResource
    {0x144460330, "hunter"},                         // DSHunterComponentResource
    {0x144469640, "octopus mech"},                   // DSMechOctopusComponentResource
    {0x1444397c0, "red samurai"},                    // DSRedSamuraiComponentResource
    {0x14445a160, "BT member"},                      // DSBTMembershipComponentResource
};
constexpr uintptr_t kPorterRecord = 0x144473870;  // DSPorterComponentResource

using CreateFn = uintptr_t (*)(uintptr_t info);
using FindFn = uintptr_t (*)(uintptr_t resource, uintptr_t record);

CreateFn g_create = nullptr;
std::atomic<bool> g_veto{false};
std::atomic<int> g_vetoed{0};
std::atomic<int> g_passed{0};

uintptr_t resourceOf(uintptr_t info) {
    const uintptr_t ref = decima::readPointer(info + kInfoResourceRef);
    uint64_t flags = 0;
    if (!ref || !decima::safeRead(ref + kRefFlags, flags) || static_cast<int64_t>(flags >> kRefLoadedShift) >= 0) return 0;
    return decima::readPointer(decima::readPointer(ref) + kHolderResource);
}

// The enemy kind of an entity resource, or null for anything else.
const char* enemyKind(uintptr_t resource) {
    if (!resource) return nullptr;
    const auto find = reinterpret_cast<FindFn>(ds2::at(kFindComponentResource));
    if (find(resource, ds2::at(kPorterRecord))) return nullptr;
    for (const EnemyKind& kind : kEnemyKinds) {
        if (find(resource, ds2::at(kind.record))) return kind.name;
    }
    return nullptr;
}

// Runs on the spawn workers as well as the simulation thread.
uintptr_t createDetour(uintptr_t info) {
    if (!g_veto.load()) return g_create(info);
    const char* kind = enemyKind(resourceOf(info));
    if (!kind) {
        if (const int passed = ++g_passed; passed % kPassedLogEvery == 0) {
            logger::write("enemy_veto: %d spawns of other things let through, %d enemies vetoed", passed, g_vetoed.load());
        }
        return g_create(info);
    }
    const int count = ++g_vetoed;
    if (count <= kMaxLogged || count % 100 == 0) {
        char uuid[kUuidSize * 2 + 1] = {};
        for (size_t i = 0; i < kUuidSize; ++i) {
            uint8_t byte = 0;
            decima::safeRead(info + kInfoEntityUuid + i, byte);
            snprintf(uuid + i * 2, 3, "%02x", byte);
        }
        logger::write("enemy_veto: vetoed %s %s (%d vetoed, %d other spawns let through)", kind, uuid, count,
                      g_passed.load());
    }
    return 0;
}

}  // namespace

namespace enemy_veto {

void installEarly() {
    hooks::install("enemy spawn veto", ds2::at(kCreateEntity), reinterpret_cast<void*>(&createDetour),
                   reinterpret_cast<void**>(&g_create));
}

}  // namespace enemy_veto

namespace game {

void vetoEnemies(bool veto) {
    if (g_veto.exchange(veto) != veto) logger::write("enemy_veto: %s", veto ? "on" : "off");
}

}  // namespace game
