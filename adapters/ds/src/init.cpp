#include "init.h"

#include "config.h"
#include "crash_dump.h"
#include "dx12_hook.h"
#include "game.h"
#include "log.h"
#include "marker_overlay.h"
#include "net_client.h"
#include "player_sync.h"

namespace {

constexpr DWORD kResolvePollMs = 1000;

// Leaked on purpose: joining the net thread from a static destructor would run under the loader lock.
NetClient& g_net = *new NetClient;

}  // namespace

DWORD WINAPI initThread(LPVOID) {
    logger::write("adapter: start (DEATH STRANDING 2)");
    crash_dump::install();
    const Config config = loadConfig();
    marker_overlay::setSelfMarker(config.selfMarker);
    if (config.overlay && !dx12_hook::install(marker_overlay::draw)) logger::write("adapter: overlay unavailable");
    while (!game::resolve()) Sleep(kResolvePollMs);
    logger::write("adapter: engine objects found, linking to the launcher on port %u", config.port);
    player_sync::start(g_net, config.port);
    return 0;
}
