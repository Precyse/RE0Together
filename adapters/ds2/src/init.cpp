#include "init.h"

#include "config.h"
#include "crash_dump.h"
#include "documents_redirect.h"
#include "dx12_hook.h"
#include "game.h"
#include "log.h"
#include "main_thread.h"
#include "marker_overlay.h"
#include "net_client.h"
#include "player_sync.h"
#include "remote_body.h"

namespace {

constexpr DWORD kResolvePollMs = 1000;

// Leaked on purpose: joining the net thread from a static destructor would run under the loader lock.
NetClient& g_net = *new NetClient;

}  // namespace

DWORD WINAPI initThread(LPVOID) {
    logger::write("adapter: start (DEATH STRANDING 2)");
    logger::write("adapter: saves go to the session folder: %s", documents_redirect::active() ? "yes" : "no");
    crash_dump::install();
    const Config config = loadConfig();
    marker_overlay::setSelfMarker(config.selfMarker);
    remote_body::setEnabled(config.remoteBody);
    if (config.overlay && !dx12_hook::install(marker_overlay::draw)) logger::write("adapter: overlay unavailable");
    while (!game::resolve()) Sleep(kResolvePollMs);
    logger::write("adapter: engine objects found, linking to the launcher on port %u", config.port);
    if (config.remoteBody && !main_thread::install(game::frameFunction(), remote_body::tick)) {
        logger::write("adapter: no simulation-thread hook, bodies off");
    }
    player_sync::start(g_net, config.port);
    return 0;
}
