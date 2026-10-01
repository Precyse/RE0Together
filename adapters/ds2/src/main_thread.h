#pragma once
#include <cstdint>

// Runs adapter work on the game's simulation thread, where engine calls that are not thread-safe (creating
// entities) belong. A function the game calls every frame on that thread (game::frameFunction) is hooked; for the
// first seconds the hook counts which thread calls it most, then runs the callback on that thread only.
namespace main_thread {

using Callback = void (*)();

// `frameFunction` takes one pointer argument and returns a pointer (DS2: Player::GetLastActivatedCamera).
bool install(uintptr_t frameFunction, Callback tick);

}  // namespace main_thread
