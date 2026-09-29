#pragma once
#include <cstddef>
#include <cstdint>

#include "game.h"

// Generated thunk for the enemy damage vtable slot: thiscall ret 0xC in, one __stdcall handler call out.
namespace damage_thunk {

constexpr size_t kThunkSize = 32;

using Handler = void(__stdcall*)(void* enemy, void* attacker, float distance, game::HitInfo* info,
                                 uintptr_t original);

// Replaces the damage slot of `vtable` with a thunk written into `memory` (kThunkSize executable bytes) that calls
// `handler` with the enemy, the three damage arguments and the replaced function. Returns that function, or 0.
uintptr_t patchVtable(uintptr_t vtable, uint8_t* memory, Handler handler);

// Calls a damage function the way the game does.
inline void callOriginal(uintptr_t original, void* enemy, void* attacker, float distance, game::HitInfo* info) {
    game::callThiscall<void>(original, enemy, attacker, distance, info);
}

}  // namespace damage_thunk
