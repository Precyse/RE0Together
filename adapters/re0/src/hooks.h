#pragma once

#include <cstdint>

// Shared MinHook wrapper: one initialization, retried installs, status logging.
namespace hooks {

// Creates and enables an inline hook at `address`. Retries while the game is still settling after SteamStub
// unpacks, and logs the MinHook status on final failure.
bool install(const char* name, uintptr_t address, void* detour, void** original);

void remove(uintptr_t address);

}  // namespace hooks
