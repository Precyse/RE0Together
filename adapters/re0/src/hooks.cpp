#include "hooks.h"

#include <windows.h>

#include <string>
#include <vector>

#include "MinHook.h"
#include "debug_stats.h"
#include "log.h"

namespace {

constexpr int kInstallAttempts = 40;
constexpr DWORD kRetryDelayMs = 250;

struct QueuedHook {
    std::string name;
    void* target;
};

// Only the batching thread touches the queue, so it needs no lock.
thread_local bool t_batching = false;
std::vector<QueuedHook> g_queued;

bool ensureInitialized() {
    const MH_STATUS status = MH_Initialize();
    if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED) return true;
    logger::write("hooks: MH_Initialize failed: %s", MH_StatusToString(status));
    return false;
}

MH_STATUS tryInstall(void* target, void* detour, void** original) {
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) return status;
    if (t_batching) return MH_QueueEnableHook(target);
    status = MH_EnableHook(target);
    return status == MH_ERROR_ENABLED ? MH_OK : status;
}

void reportFailure(const char* name, void* target, MH_STATUS status) {
    logger::write("hooks: %s failed at %p: %s", name, target, MH_StatusToString(status));
    debug_stats::setError("hook %s: %s", name, MH_StatusToString(status));
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
            if (t_batching) g_queued.push_back({name, target});
            logger::write("hooks: %s %s at %p (attempt %d)", name, t_batching ? "queued" : "hooked", target, attempt);
            return true;
        }
        Sleep(kRetryDelayMs);
    }
    reportFailure(name, target, status);
    return false;
}

void beginBatch() { t_batching = true; }

void applyBatch() {
    t_batching = false;
    const MH_STATUS status = MH_ApplyQueued();
    if (status == MH_OK) {
        logger::write("hooks: %zu queued hooks enabled", g_queued.size());
    } else {
        logger::write("hooks: batch enable stopped: %s; enabling the rest one by one", MH_StatusToString(status));
        for (const QueuedHook& hook : g_queued) {
            const MH_STATUS single = MH_EnableHook(hook.target);
            if (single == MH_OK || single == MH_ERROR_ENABLED) continue;
            MH_QueueDisableHook(hook.target);
            reportFailure(hook.name.c_str(), hook.target, single);
        }
    }
    g_queued.clear();
}

void remove(uintptr_t address) {
    void* target = reinterpret_cast<void*>(address);
    // Inside a batch the hook is not enabled yet: take back the queued enable instead.
    if (t_batching) {
        MH_QueueDisableHook(target);
    } else {
        MH_DisableHook(target);
    }
}

}  // namespace hooks
