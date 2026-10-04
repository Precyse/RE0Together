// DEATH STRANDING 2: every gameplay damage is queued by 0x140129de0(victim, DamageParams*) and applied later by
// EntityManagerGame::ApplyDamage. A damage whose instigator (DamageParams +0x20, a weak pointer to an entity) is the
// partner's body, or a child of it such as its weapon, is not queued. Damage that combat sync applies on the body's
// behalf is made under remote_apply::Scope straight through ApplyDamage and is not affected.
#include "ds2/damage_veto.h"

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/remote_player.h"
#include "hooks.h"
#include "remote_apply.h"

namespace {

constexpr uintptr_t kQueueDamage = 0x140129de0;
constexpr uintptr_t kParamsInstigator = 0x20;

using QueueFn = void (*)(uintptr_t victim, uintptr_t params);
QueueFn g_original = nullptr;

bool fromRemoteBody(uintptr_t params) {
    const uintptr_t body = remote_player::entity();
    if (!body || !params) return false;
    const uintptr_t instigator = ds2::weakEntity(params + kParamsInstigator);
    return instigator && (instigator == body || decima::readPointer(instigator + ds2::kEntityParent) == body);
}

void queueDetour(uintptr_t victim, uintptr_t params) {
    if (!remote_apply::active() && fromRemoteBody(params)) return;
    g_original(victim, params);
}

}  // namespace

namespace damage_veto {

void installEarly() {
    hooks::install("damage queue", ds2::at(kQueueDamage), reinterpret_cast<void*>(&queueDetour),
                   reinterpret_cast<void**>(&g_original));
}

}  // namespace damage_veto
