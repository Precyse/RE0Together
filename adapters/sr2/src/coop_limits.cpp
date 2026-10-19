#include "coop_limits.h"

#include <atomic>

#include "coop_pools.h"
#include "hooks.h"
#include "limit_thunk.h"
#include "replication_switch.h"
#include "sr2_addresses.h"
#include "vehicle_pool.h"

namespace {

using NetSetModeFn = void(__cdecl*)(int mode);

NetSetModeFn g_original = nullptr;
uint32_t g_maxPlayers = sr2::kCoopDefaultPlayers;
std::atomic<int> g_running{0};

void __cdecl detourNetSetMode(int mode) {
    if (mode == sr2::kNetModeCoop) {
        if (coop_pools::resize(g_maxPlayers, vehicle_pool::wanted())) {
            vehicle_pool::install();
            replication_switch::enable(vehicle_pool::installed());
        }
    }
    ++g_running;
    g_original(mode);
    --g_running;
    vehicle_pool::onModeSet(mode);
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
