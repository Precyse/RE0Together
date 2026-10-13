#include "coop_limits.h"

#include <atomic>

#include "hooks.h"
#include "log.h"
#include "sr2_addresses.h"

namespace {

using NetSetModeFn = void(__cdecl*)(int mode);

NetSetModeFn g_original = nullptr;
uint32_t g_maxPlayers = sr2::kCoopDefaultPlayers;
std::atomic<int> g_running{0};

uint32_t& limit(uintptr_t address) { return *reinterpret_cast<uint32_t*>(address); }

void raiseLimits() {
    const uint32_t oldSession = limit(sr2::kMaxSessionPlayers);
    const uint32_t oldPlayers = limit(sr2::kMaxPlayers);
    limit(sr2::kMaxSessionPlayers) = g_maxPlayers;
    limit(sr2::kMaxPlayers) = g_maxPlayers;
    logger::write("coop limits: max session players %u -> %u, max players %u -> %u", oldSession, g_maxPlayers,
                  oldPlayers, g_maxPlayers);
}

void __cdecl detourNetSetMode(int mode) {
    ++g_running;
    g_original(mode);
    --g_running;
    if (mode == sr2::kNetModeCoop) raiseLimits();
}

}  // namespace

namespace coop_limits {

bool install(uint32_t maxPlayers) {
    g_maxPlayers = maxPlayers;
    return hooks::install("net_set_mode", sr2::kNetSetMode, reinterpret_cast<void*>(&detourNetSetMode),
                          reinterpret_cast<void**>(&g_original));
}

bool insideNetSetMode() { return g_running.load() > 0; }

}  // namespace coop_limits
