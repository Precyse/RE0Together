#pragma once
#include <cstddef>
#include <cstdint>

#include "game.h"

// Generated thunk for the enemy update vtable slot: thiscall without arguments in, one __stdcall handler call out.
namespace update_thunk {

constexpr size_t kThunkSize = 16;

using Handler = void(__stdcall*)(void* enemy, uintptr_t original);

// Replaces the update slot of `vtable` with a thunk written into `memory` (kThunkSize executable bytes) that calls
// `handler` with the enemy and the replaced function. Returns that function, or 0.
uintptr_t patchVtable(uintptr_t vtable, uint8_t* memory, Handler handler);

// Runs an update function the way the game does.
inline void callOriginal(uintptr_t original, void* enemy) { game::callThiscall<void>(original, enemy); }

}  // namespace update_thunk
