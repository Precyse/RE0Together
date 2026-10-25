#pragma once
#include <windows.h>

#include <cstdint>

// The game's own resource and equip calls that the weapon re-equip (equip_refresh) and the outfit swap (appearance)
// repeat from the menu close (0x5d8040), and a peer's floor put (floor_items_sync) from the drop menu: all run on the
// game thread and must be wrapped in `guarded`.
namespace equip_steps {

// Runs `step` and returns false when it raised an exception.
template <class Step>
bool guarded(Step step) {
    __try {
        step();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// sGameChara, or null.
void* gameChara();

// The character's item block (0x50dc70) and its equipped slot, or null.
void* itemBlock(uintptr_t player, uint32_t& slot);

// Plain gameplay (no menu, no door) with the player in the loaded room; a change that arrives earlier waits.
bool inPlainGameplay(uintptr_t player);

// Releases / requests a resource set of sGameChara (kind: weapon unit or character; type: weapon type).
void releaseSet(void* chara, int32_t kind, int32_t type);
void requestSet(void* chara, int32_t kind, int32_t type, uint32_t record);

// True once every requested set has loaded (each call drives the loads).
bool requestsLoaded();

// The menu close's equip step for one player (0x5d7785..0x5d77ab).
void equip(uintptr_t player);

// The weapon type the player holds, or equip_rule::kNoWeapon.
int32_t heldType(uintptr_t player);

}  // namespace equip_steps
