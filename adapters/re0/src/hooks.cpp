#include "hooks.h"

#include <windows.h>

#include "MinHook.h"
#include "debug_stats.h"
#include "log.h"

namespace {

constexpr int kInstallAttempts = 40;
constexpr DWORD kRetryDelayMs = 250;

bool ensureInitialized() {
    const MH_STATUS status = MH_Initialize();
    if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED) return true;
    logger::write("hooks: MH_Initialize failed: %s", MH_StatusToString(status));
    return false;
}

MH_STATUS tryInstall(void* target, void* detour, void** original) {
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) return status;
    status = MH_EnableHook(target);
    return status == MH_ERROR_ENABLED ? MH_OK : status;
}

}  // namespace

namespace hooks {

bool install(const char* name, uintptr_t address, void* detour, void** original) {
    if (!ensureInitialized()) return false;
    void* target = reinterpret_cast<void*>(address);
    MH_STATUS status = MH_UNKNOWN;
    for (int attempt = 1; attempt <= kInstallAttempts; ++attempt) {
        status = tryInstall(target, detour, original);
        if (status == MH_OK) {
            logger::write("hooks: %s hooked at 0x%08x (attempt %d)", name, static_cast<unsigned>(address), attempt);
            return true;
        }
        Sleep(kRetryDelayMs);
    }
    logger::write("hooks: %s failed at 0x%08x: %s", name, static_cast<unsigned>(address), MH_StatusToString(status));
    debug_stats::setError("hook %s: %s", name, MH_StatusToString(status));
    return false;
}

void remove(uintptr_t address) { MH_DisableHook(reinterpret_cast<void*>(address)); }

}  // namespace hooks
