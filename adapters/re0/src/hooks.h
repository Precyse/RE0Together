#pragma once

#include <cstdint>

// Shared MinHook wrapper: one initialization, retried installs, status logging.
namespace hooks {

// Creates and enables an inline hook at `address`. Retries while the game is still settling after SteamStub
// unpacks, and logs the MinHook status on final failure.
bool install(const char* name, uintptr_t address, void* detour, void** original);

// Between beginBatch and applyBatch, installs on the calling thread create their hooks and queue the enables;
// applyBatch enables them all in one thread suspension, logging any that fail and keeping the rest.
void beginBatch();
void applyBatch();

// Disables the hook at `address`; inside a batch, takes back its queued enable.
void remove(uintptr_t address);

}  // namespace hooks
