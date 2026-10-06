#pragma once
#include <cstddef>
#include <cstdint>

#include "game.h"

// Generated thunk for the enemy setAction vtable slot: thiscall with N stack arguments in (N = 1, 2 or 4 depending on
// the class), one __stdcall handler call out. The handler gets a pointer to the caller's arguments and decides
// whether the original runs; the thunk pops the N arguments itself either way.
namespace set_action_thunk {

constexpr size_t kThunkSize = 32;

using Handler = void(__stdcall*)(void* enemy, uintptr_t original, const int32_t* args);

// Replaces the setAction slot of `vtable` with a thunk written into `memory` (kThunkSize executable bytes) for a
// function that takes `argc` arguments. Returns the replaced function, or 0.
uintptr_t patchVtable(uintptr_t vtable, uint8_t* memory, size_t argc, Handler handler);

}  // namespace set_action_thunk
