#include "coop_limits.h"

#include <atomic>

#include "coop_pools.h"
#include "hooks.h"
#include "limit_thunk.h"
#include "replication_switch.h"
#include "sr2_addresses.h"

namespace {

using NetSetModeFn = void(__cdecl*)(int mode);

NetSetModeFn g_original = nullptr;
uint32_t g_maxPlayers = sr2::kCoopDefaultPlayers;
std::atomic<int> g_running{0};

void __cdecl detourNetSetMode(int mode) {
    if (mode == sr2::kNetModeCoop && coop_pools::resize(g_maxPlayers)) replication_switch::enable();
    ++g_running;
    g_original(mode);
    --g_running;
}

}  // namespace

namespace coop_limits {

bool install(uint32_t maxPlayers) {
    g_maxPlayers = maxPlayers;
    return hooks::install("net_set_mode", sr2::kNetSetMode, reinterpret_cast<void*>(&detourNetSetMode),
                          reinterpret_cast<void**>(&g_original)) &&
           limit_thunk::install(maxPlayers);
}

bool insideNetSetMode() { return g_running.load() > 0; }

}  // namespace coop_limits
