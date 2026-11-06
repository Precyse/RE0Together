#include "puppet_brain.h"

#include <array>
#include <atomic>

#include "enemy_mirror.h"
#include "hook_registry.h"
#include "log.h"
#include "names.h"
#include "puppet_registry.h"
#include "refw.h"

namespace {

// REFramework hook arguments: argv[0] the VM context, argv[1] `this`, then the method's parameters, 8 bytes each.
constexpr int kThis = 1;
constexpr int kFirstParam = 2;
constexpr uint64_t kSummaryPeriodMs = 30000;
constexpr uint32_t kHolderCharaId = 0x14C;  // NPCHolder.CharaID

enum Layer : size_t { kNavigation, kAction, kLocomotion, kHp, kCleanup, kLayers };
constexpr const char* kLayerNames[kLayers] = {"navigation", "action", "locomotion", "hp", "cleanup"};

std::array<std::atomic<uint32_t>, kLayers> g_refused{};
bool g_installed = false;
bool g_mirrorInstalled = false;
uint64_t g_lastSummaryMs = 0;
thread_local void* t_readyCharacter = nullptr;

void* arg(int argc, void** argv, int index) { return argc > index ? argv[index] : nullptr; }

uint32_t idArg(int argc, void** argv) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg(argc, argv, kFirstParam)));
}

int refuse(Layer layer) {
    g_refused[layer].fetch_add(1, std::memory_order_relaxed);
    return REFRAMEWORK_HOOK_SKIP_ORIGINAL;
}

// A body driven from a stream: our puppets and the enemy mirrors.
bool remote(const void* object) { return puppet_registry::isPuppetObject(object) || enemy_mirror::isMirrorObject(object); }

// A request on one of a remote body's controllers that our driver did not make.
int gate(int argc, void** argv, Layer layer) {
    if (remote(arg(argc, argv, kThis)) && !puppet_registry::inDriverScope()) return refuse(layer);
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

int preReady(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    t_readyCharacter = arg(argc, argv, kFirstParam);
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

void postReady(void**, REFrameworkTypeDefinitionHandle, unsigned long long) {
    puppet_registry::onReady(t_readyCharacter);
    t_readyCharacter = nullptr;
}

int preDestroy(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    puppet_registry::onDestroyed(arg(argc, argv, kThis));
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

int preNavigation(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) { return gate(argc, argv, kNavigation); }
int preAction(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) { return gate(argc, argv, kAction); }
int preLocomotion(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) { return gate(argc, argv, kLocomotion); }
int preHp(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) { return gate(argc, argv, kHp); }

// A hit landing on an enemy mirror is the host's to apply: the mirror reports it (ENEMY_HIT when our own bodies dealt it).
int preDamageHp(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    void* hit = arg(argc, argv, kThis);
    if (enemy_mirror::isMirrorObject(hit)) {
        enemy_mirror::onDamage(hit, arg(argc, argv, kFirstParam));
        return refuse(kHp);
    }
    return gate(argc, argv, kHp);
}

int preCleanup(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    return puppet_registry::isPuppetId(idArg(argc, argv)) ? refuse(kCleanup) : REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

int preDestroyStrayPawn(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    void* holder = arg(argc, argv, kFirstParam);
    if (holder && puppet_registry::isPuppetId(refw::field<uint32_t>(holder, kHolderCharaId))) return refuse(kCleanup);
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

}  // namespace

namespace puppet_brain {

bool install() {
    const hook_registry::Handler handlers[] = {
        hook_registry::make<preReady, postReady>(&names::kReadyCharacter),
        hook_registry::make<preDestroy>(&names::kCharacterDestroy),
        hook_registry::make<preNavigation>(&names::kNavDestination),
        hook_registry::make<preNavigation>(&names::kPawnNavDestination),
        hook_registry::make<preAction>(&names::kActionCore),
        hook_registry::make<preLocomotion>(&names::kLocomotion),
        hook_registry::make<preLocomotion>(&names::kIdle),
        hook_registry::make<preDamageHp>(&names::kDamageHp),
        hook_registry::make<preHp>(&names::kSetHp),
        hook_registry::make<preCleanup>(&names::kLeaveStrayPawn),
        hook_registry::make<preDestroyStrayPawn>(&names::kDestroyStrayPawn),
        hook_registry::make<preCleanup>(&names::kPawnLost),
    };
    g_installed = hook_registry::installSet(names::kPuppet, handlers);
    return g_installed;
}

bool installed() { return g_installed; }

bool installMirror() {
    if (!g_installed) return false;
    const hook_registry::Handler handlers[] = {
        hook_registry::make<preNavigation>(&names::kMonsterNavDestination),
        hook_registry::make<preLocomotion>(&names::kMonsterLocomotion),
        hook_registry::make<preLocomotion>(&names::kMonsterIdle),
    };
    g_mirrorInstalled = hook_registry::installSet(names::kMirror, handlers);
    return g_mirrorInstalled;
}

void tick(uint64_t nowMs) {
    if (nowMs - g_lastSummaryMs < kSummaryPeriodMs) return;
    g_lastSummaryMs = nowMs;
    for (size_t layer = 0; layer < kLayers; ++layer) {
        const uint32_t refused = g_refused[layer].exchange(0, std::memory_order_relaxed);
        if (refused) logger::write("puppet_brain: %s refused %u times in 30 s", kLayerNames[layer], refused);
    }
}

}  // namespace puppet_brain
