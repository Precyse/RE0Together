#include "enemy_damage_hook.h"

#include <windows.h>

#include <array>

#include "character_owner.h"
#include "damage_thunk.h"
#include "enemy_net.h"
#include "enemy_registry.h"
#include "log.h"
#include "net_pad.h"
#include "split_rooms.h"

namespace {

using character_owner::Character;

struct Patched {
    uintptr_t vtable;
    uintptr_t original;
};

std::array<Patched, game::kEnemyVtables.size()> g_patched{};
size_t g_patchedCount = 0;
bool g_applyingNetworkHit = false;  // game thread only

uintptr_t originalFor(uintptr_t vtable) {
    for (size_t i = 0; i < g_patchedCount; ++i) {
        if (g_patched[i].vtable == vtable) return g_patched[i].original;
    }
    return 0;
}

// Hit by a player character: the owning machine reports it and the host applies it.
void onPlayerHit(Character shooter, void* enemy, void* attacker, float distance, game::HitInfo* info,
                 uintptr_t original) {
    const uintptr_t enemyAddress = reinterpret_cast<uintptr_t>(enemy);
    if (character_owner::isRemoteOwned(shooter)) return;
    if (!character_owner::isLocalOwned(shooter)) {
        damage_thunk::callOriginal(original, enemy, attacker, distance, info);
    } else if (split_rooms::localEnemyAuthority()) {
        damage_thunk::callOriginal(original, enemy, attacker, distance, info);
        enemy_net::announceHit(enemyAddress, shooter, distance, *info);
    } else if (!enemy_net::requestHit(enemyAddress, shooter, distance, *info)) {
        damage_thunk::callOriginal(original, enemy, attacker, distance, info);
    }
}

void __stdcall onDamage(void* enemy, void* attacker, float distance, game::HitInfo* info, uintptr_t original) {
    if (g_applyingNetworkHit || !net_pad::active()) {
        damage_thunk::callOriginal(original, enemy, attacker, distance, info);
        return;
    }
    const Character shooter = character_owner::identify(reinterpret_cast<uintptr_t>(attacker));
    if (shooter != Character::Unknown) {
        onPlayerHit(shooter, enemy, attacker, distance, info, original);
        return;
    }
    const bool hasSlot = enemy_registry::slotOf(reinterpret_cast<uintptr_t>(enemy)) != enemy_registry::kNoSlot;
    if (split_rooms::localEnemyAuthority() || !hasSlot) damage_thunk::callOriginal(original, enemy, attacker, distance, info);
}

}  // namespace

namespace enemy_damage_hook {

bool install() {
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, game::kEnemyVtables.size() * damage_thunk::kThunkSize,
                                                      MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!memory) return false;
    for (const uintptr_t vtable : game::kEnemyVtables) {
        const uintptr_t original =
            damage_thunk::patchVtable(vtable, memory + g_patchedCount * damage_thunk::kThunkSize, onDamage);
        if (original) g_patched[g_patchedCount++] = {vtable, original};
    }
    logger::write("enemy_damage_hook: patched %zu of %zu vtables", g_patchedCount, game::kEnemyVtables.size());
    return g_patchedCount == game::kEnemyVtables.size();
}

bool applyNetworkHit(uintptr_t enemy, uintptr_t attacker, float distance, game::HitInfo& info) {
    const uintptr_t original = originalFor(game::readPointer(enemy));
    if (!original) return false;
    g_applyingNetworkHit = true;
    damage_thunk::callOriginal(original, reinterpret_cast<void*>(enemy), reinterpret_cast<void*>(attacker), distance,
                               &info);
    g_applyingNetworkHit = false;
    return true;
}

}  // namespace enemy_damage_hook
