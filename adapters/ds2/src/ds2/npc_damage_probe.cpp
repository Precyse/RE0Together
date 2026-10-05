// DEATH STRANDING 2: the NPC damage probe (see ds2/npc_damage_probe.h). DSNpcDamageComponentBase's MsgDamage handler
// 0x1418b2370(component, message) (the reflection table points at the jump thunk 0x1418b4850) refuses a hit when the
// victim entity ([component +0x50], flags +0x98 bit 9) is asleep, when the component's invincible bytes (+0x58 through
// a virtual, +0x59) are set, or at the attack context's gates (tools/ds2/out/analysis/ENEMIES.md, section 4). The
// DamageParams of the message are at message +0x20.
#include "ds2/npc_damage_probe.h"

#include <windows.h>

#include <cstdio>

#include "decima/safe_read.h"
#include "ds2/combat_log.h"
#include "ds2/damage_params.h"
#include "ds2/engine.h"
#include "ds2/remote_player.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kMsgDamageHandler = 0x1418b2370;
// DSBanditDamageComponent's damage result step 0x141a45360(component, info): applies the damage the earlier steps put in
// the info struct to the life block (its writer is 0x141887480, found with the health watch). It returns at once when
// info +0xFA is 0.
constexpr uintptr_t kApplyResult = 0x141a45360;
constexpr uintptr_t kInfoDamage = 0x00, kInfoStagger = 0x04, kInfoFlags = 0xA0, kInfoType = 0xA8, kInfoApplies = 0xFA;
constexpr uintptr_t kComponentEntity = 0x50;
constexpr uintptr_t kComponentInvincible = 0x58, kComponentGuarded = 0x59;
constexpr uintptr_t kMessageParams = 0x20;
constexpr uintptr_t kEntityFlags = 0x98;
constexpr unsigned kAsleepBit = 9;
constexpr size_t kStackFrames = 3;
constexpr size_t kOutcomeSize = 200;

using HandlerFn = uintptr_t (*)(uintptr_t component, uintptr_t message);
using ResultFn = uintptr_t (*)(uintptr_t component, uintptr_t info);
HandlerFn g_original = nullptr;
ResultFn g_originalResult = nullptr;

uintptr_t resultDetour(uintptr_t component, uintptr_t info) {
    const uintptr_t victim = component ? decima::readPointer(component + kComponentEntity) : 0;
    if (combat_log::isTracked(victim) && info) {
        float damage = 0, stagger = 0;
        uint64_t flags = 0;
        uint16_t type = 0;
        uint8_t applies = 0;
        decima::safeRead(info + kInfoDamage, damage);
        decima::safeRead(info + kInfoStagger, stagger);
        decima::safeRead(info + kInfoFlags, flags);
        decima::safeRead(info + kInfoType, type);
        decima::safeRead(info + kInfoApplies, applies);
        logger::write("npc_apply: info damage %g stagger %g flags %llx type %x applies %u", damage, stagger,
                      static_cast<unsigned long long>(flags), type, applies);
    }
    return g_originalResult(component, info);
}

uintptr_t detour(uintptr_t component, uintptr_t message) {
    const uintptr_t victim = component ? decima::readPointer(component + kComponentEntity) : 0;
    const uintptr_t params = message + kMessageParams;
    const combat_log::Snapshot snapshot = combat_log::before(victim, params);
    if (!snapshot.tracked) return g_original(component, message);
    void* frames[kStackFrames] = {};
    const USHORT captured = RtlCaptureStackBackTrace(1, kStackFrames, frames, nullptr);
    uint64_t flags = 0;
    decima::safeRead(victim + kEntityFlags, flags);
    const uint8_t invincible = ds2::field<uint8_t>(component, kComponentInvincible);
    const uint8_t guarded = ds2::field<uint8_t>(component, kComponentGuarded);
    const uintptr_t result = g_original(component, message);
    char outcome[kOutcomeSize];
    std::snprintf(outcome, sizeof(outcome), "NPC damage handler from %p %p %p, asleep %d, invincible %u, guarded %u, ours %d",
                  captured > 0 ? frames[0] : nullptr, captured > 1 ? frames[1] : nullptr, captured > 2 ? frames[2] : nullptr,
                  static_cast<int>((flags >> kAsleepBit) & 1), invincible, guarded,
                  snapshot.attacker == remote_player::entity());
    combat_log::after(snapshot, victim, params, outcome);
    return result;
}

}  // namespace

namespace npc_damage_probe {

void installEarly() {
    hooks::install("npc damage handler", ds2::at(kMsgDamageHandler), reinterpret_cast<void*>(&detour),
                   reinterpret_cast<void**>(&g_original));
    hooks::install("npc damage result", ds2::at(kApplyResult), reinterpret_cast<void*>(&resultDetour),
                   reinterpret_cast<void**>(&g_originalResult));
}

}  // namespace npc_damage_probe
