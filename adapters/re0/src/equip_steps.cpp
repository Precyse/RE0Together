#include "equip_steps.h"

#include "equip_rule.h"
#include "game.h"
#include "game_state.h"

namespace equip_steps {

void* gameChara() { return reinterpret_cast<void*>(game::readPointer(game::kGameCharaGlobal)); }

void* itemBlock(uintptr_t player, uint32_t& slot) {
    void* block = game::callThiscall<void*>(game::kPlayerItemBlockFunction, reinterpret_cast<void*>(player));
    if (!block || !game::readMemory(reinterpret_cast<uintptr_t>(block) + game::kBlockEquippedOffset, slot)) return nullptr;
    return block;
}

bool inPlainGameplay(uintptr_t player) {
    return game_state::playing() && !game_state::menuOpen() && game_state::inCurrentRoom(player);
}

void releaseSet(void* chara, int32_t kind, int32_t type) {
    game::callThiscall<void>(game::kReleaseSetFunction, chara, kind, type);
}

void requestSet(void* chara, int32_t kind, int32_t type, uint32_t record) {
    game::callThiscall<void>(game::kRequestSetFunction, chara, kind, type, record);
}

bool requestsLoaded() {
    void* chara = gameChara();
    return chara && game::callThiscall<bool>(game::kRequestsLoadedFunction, chara);
}

void equip(uintptr_t player) {
    auto* self = reinterpret_cast<void*>(player);
    uint32_t slot = equip_rule::kNoSlot;
    void* block = itemBlock(player, slot);
    if (!block) return;
    const int32_t type = game::callThiscall<int32_t>(game::kBlockSetEquippedFunction, block, slot);
    game::callThiscall<void>(game::kPlayerSetWeaponTypeFunction, self, type);
    if (slot == equip_rule::kNoSlot) return;
    game::callThiscall<void>(game::kPlayerWeaponAttachFunction, self);
    game::callThiscall<void>(game::kPlayerWeaponAimFunction, self);
}

int32_t heldType(uintptr_t player) {
    int32_t type = equip_rule::kNoWeapon;
    game::readMemory(player + game::kPlayerWeaponTypeOffset, type);
    return type;
}

}  // namespace equip_steps
