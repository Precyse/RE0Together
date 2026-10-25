#include "contact_game.h"

#include "equip_steps.h"
#include "game.h"

namespace contact_game {

bool entryState(uint32_t kind, uint32_t level, int32_t& state) {
    if (kind >= contact_rule::kReactionKindCount) return false;
    uint32_t begin = 0;
    uint32_t end = 0;
    const uintptr_t row = game::kReactionTable + kind * game::kReactionRowSize;
    if (!game::readMemory(row, begin) || !game::readMemory(row + sizeof(begin), end) || begin == 0) return false;
    const uintptr_t entry = static_cast<uintptr_t>(begin) + level * game::kReactionEntrySize;
    return entry < end && game::readMemory(entry, state);
}

bool relativePoint(uintptr_t enemy, uintptr_t pointAddress, float (&out)[3]) {
    float enemyPos[3];
    if (!game::readMemory(enemy + game::kUnitPositionOffset, enemyPos) || !game::readMemory(pointAddress, out)) return false;
    for (int i = 0; i < 3; ++i) out[i] -= enemyPos[i];
    return true;
}

bool victimView(uintptr_t victim, contact_rule::VictimView& out) {
    const uintptr_t think = game::readPointer(victim + game::kPlayerThinkOffset);
    return think && game::readMemory(think + game::kThinkGroupOffset, out.group) &&
           game::readMemory(think + game::kThinkStateOffset, out.state) && game::readMemory(victim + game::kPlayerHpOffset, out.hp);
}

bool freeVictim(uintptr_t victim) {
    return equip_steps::guarded([&] { game::callThiscall<void>(game::kPlayerResetFunction, reinterpret_cast<void*>(victim)); });
}

}  // namespace contact_game
