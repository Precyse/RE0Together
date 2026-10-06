#include "equip_refresh.h"

#include <windows.h>

#include "equip_rule.h"
#include "game.h"

namespace {

// The sequence of the menu-close equip step (0x5d7785..0x5d77ab) for one player.
void runUnguarded(uintptr_t player) {
    auto* self = reinterpret_cast<void*>(player);
    void* block = game::callThiscall<void*>(game::kPlayerItemBlockFunction, self);
    uint32_t slot = equip_rule::kNoSlot;
    if (!block || !game::readMemory(reinterpret_cast<uintptr_t>(block) + game::kBlockEquippedOffset, slot)) return;
    const uint32_t type = game::callThiscall<uint32_t>(game::kBlockSetEquippedFunction, block, slot);
    game::callThiscall<void>(game::kPlayerSetWeaponTypeFunction, self, type);
    if (slot == equip_rule::kNoSlot) return;
    game::callThiscall<void>(game::kPlayerWeaponAttachFunction, self);
    game::callThiscall<void>(game::kPlayerWeaponAimFunction, self);
}

}  // namespace

namespace equip_refresh {

bool run(uintptr_t player) {
    __try {
        runUnguarded(player);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace equip_refresh
