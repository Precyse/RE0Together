// DEATH STRANDING 2: log-only damage instruments (see ds2/damage_diag.h).
#include "ds2/damage_diag.h"

#include <atomic>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/entity_lookup.h"
#include "enemy_directory.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kPreProcess = 0x141a45200;  // DSBanditDamageComponent (component, info): decides whether the hit counts
constexpr uintptr_t kResultStep = 0x141a45360;  // (component, info): applies the damage the info holds to the life block
constexpr uintptr_t kComponentEntity = 0x50, kComponentAi = 0xA00, kAiIndividual = 0x1F0, kIndividualState = 0x2D8,
                    kIndividualFlags = 0x2DC;
constexpr uintptr_t kComponentHandled = 0x187A, kComponentStance = 0xABC, kComponentGuard = 0x538;
constexpr uintptr_t kInfoDamage = 0x00, kInfoStagger = 0x04, kInfoFlags = 0xA0, kInfoType = 0xA8, kInfoApplies = 0xFA;
constexpr uint32_t kMaxLogged = 400;

std::atomic<uint32_t> g_logged{0};

using StepFn = uintptr_t (*)(uintptr_t component, uintptr_t info);
StepFn g_originalPre = nullptr;
StepFn g_originalResult = nullptr;

bool trackedEnemy(uintptr_t component, uintptr_t& victim) {
    victim = component ? decima::readPointer(component + kComponentEntity) : 0;
    enemy_directory::Uuid uuid;
    return victim && ds2::entityUuid(victim, uuid) && enemy_directory::netIdOf(uuid) != 0;
}

struct State {
    uint32_t ai = 0, aiFlags = 0;
    uint8_t handled = 0, stance = 0, guard = 0;
};

State stateOf(uintptr_t component) {
    State state;
    const uintptr_t ai = decima::readPointer(component + kComponentAi);
    const uintptr_t individual = ai ? decima::readPointer(ai + kAiIndividual) : 0;
    if (individual) {
        decima::safeRead(individual + kIndividualState, state.ai);
        decima::safeRead(individual + kIndividualFlags, state.aiFlags);
    }
    decima::safeRead(component + kComponentHandled, state.handled);
    decima::safeRead(component + kComponentStance, state.stance);
    decima::safeRead(component + kComponentGuard, state.guard);
    return state;
}

uintptr_t preDetour(uintptr_t component, uintptr_t info) {
    uintptr_t victim = 0;
    if (!trackedEnemy(component, victim) || !info || g_logged.load() >= kMaxLogged) return g_originalPre(component, info);
    const State before = stateOf(component);
    uint16_t type = 0;
    uint64_t flags = 0;
    decima::safeRead(info + kInfoType, type);
    decima::safeRead(info + kInfoFlags, flags);
    const uintptr_t result = g_originalPre(component, info);
    uint8_t applies = 0;
    decima::safeRead(info + kInfoApplies, applies);
    const State after = stateOf(component);
    ++g_logged;
    logger::write("damage_diag: pre-process of a type %x hit (info flags %llx): AI state %u flags %x, stance %u, guard %u, handled %u; applies %u, handled %u afterwards",
                  type, static_cast<unsigned long long>(flags), before.ai, before.aiFlags, before.stance, before.guard, before.handled,
                  applies, after.handled);
    return result;
}

uintptr_t resultDetour(uintptr_t component, uintptr_t info) {
    uintptr_t victim = 0;
    if (trackedEnemy(component, victim) && info && g_logged.load() < kMaxLogged) {
        float damage = 0, stagger = 0;
        uint16_t type = 0;
        uint8_t applies = 0;
        decima::safeRead(info + kInfoDamage, damage);
        decima::safeRead(info + kInfoStagger, stagger);
        decima::safeRead(info + kInfoType, type);
        decima::safeRead(info + kInfoApplies, applies);
        ++g_logged;
        logger::write("damage_diag: result step: damage %g, stagger %g, type %x, applies %u, handled %u", damage, stagger, type, applies,
                      stateOf(component).handled);
    }
    return g_originalResult(component, info);
}

}  // namespace

namespace damage_diag {

void installEarly() {
    hooks::install("damage pre-process", ds2::at(kPreProcess), reinterpret_cast<void*>(&preDetour), reinterpret_cast<void**>(&g_originalPre));
    hooks::install("damage result step", ds2::at(kResultStep), reinterpret_cast<void*>(&resultDetour),
                   reinterpret_cast<void**>(&g_originalResult));
}

}  // namespace damage_diag
