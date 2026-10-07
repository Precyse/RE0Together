#include "auto_join.h"
#include "init.h"

#include <cstring>
#include <span>

#include "camera_parity.h"
#include "character_owner.h"
#include "command_input.h"
#include "config.h"
#include "crash_dump.h"
#include "debug_overlay.h"
#include "door_sync.h"
#include "door_travel.h"
#include "enemy_damage_hook.h"
#include "enemy_net.h"
#include "enemy_spawn.h"
#include "enemy_state.h"
#include "enemy_target.h"
#include "enemy_decision.h"
#include "room_gate.h"
#include "event_place.h"
#include "event_sync.h"
#include "flag_sync.h"
#include "floor_items_sync.h"
#include "game.h"
#include "game_tick.h"
#include "inventory_sync.h"
#include "input_record.h"
#include "input_redirect.h"
#include "join_sync.h"
#include "log.h"
#include "menu_mirror.h"
#include "net_client.h"
#include "net_pad.h"
#include "net_trace.h"
#include "pad_commands.h"
#include "partner_think.h"
#include "party_mode.h"
#include "pickup_guard.h"
#include "player_damage.h"
#include "protocol.h"
#include "resync.h"
#include "save_redirect.h"
#include "session_slot.h"
#include "split_rooms.h"
#include "state_correction.h"
#include "state_sync.h"
#include "vtable_tracer.h"

namespace {

constexpr DWORD kDecryptPollMs = 100;
constexpr DWORD kDecryptTimeoutMs = 60'000;

// Leaked on purpose: joining the net thread from a static destructor would run under the loader lock.
NetClient& g_net = *new NetClient;

bool waitForDecryption() {
    for (DWORD waited = 0; waited < kDecryptTimeoutMs; waited += kDecryptPollMs) {
        if (game::codeDecrypted()) return true;
        Sleep(kDecryptPollMs);
    }
    return false;
}

// Registration order is execution order within a frame: ownership first, then the think swap, then the remote pad advance.
void enableCoop() {
    if (!input_record::captureOriginals()) logger::write("adapter: cannot read the pad vtable, input sync disabled");
    character_owner::enable(g_net);
    inventory_sync::enable(g_net);
    floor_items_sync::enable(g_net);
    pickup_guard::enable();
    door_travel::enable(g_net);
    if (!door_sync::enable(g_net)) logger::write("adapter: door sync unavailable");
    if (!room_gate::install()) logger::write("adapter: door barrier unavailable");
    split_rooms::enable();
    flag_sync::enable(g_net);
    join_sync::enable(g_net);
    resync::enable(g_net);
    event_place::enable(g_net);
    if (!event_sync::enable(g_net)) logger::write("adapter: event sync unavailable");
    if (!session_slot::enable()) logger::write("adapter: session slot unavailable");
    enemy_net::enable(g_net);
    enemy_state::enable(g_net);
    command_input::enable();
    pad_commands::install();
    party_mode::enable(g_net);
    camera_parity::enable();
    partner_think::enable();
    game_tick::addCallback("net_pad", net_pad::advance);
    input_record::enable(g_net);
    state_correction::enable();
    enemy_damage_hook::install();
    enemy_target::install();
    enemy_decision::install();
    enemy_spawn::install();
    player_damage::install(g_net);
    menu_mirror::enable();
}

// The host's new save goes to the guests (its launcher re-sends the profile's save files), so a continue after a
// game over loads the same state on every machine.
void reportCloudWrite(const char* name) {
    if (!character_owner::isHost() || !net_pad::active()) return;
    const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(name), std::strlen(name));
    if (g_net.send(proto::kSaveChanged, true, proto::kSlotAll, bytes)) logger::write("adapter: save %s written, sharing it", name);
}

void startSubsystems() {
    logger::write("adapter: starting");
    crash_dump::install();
    const Config config = loadConfig();
    // Before the decryption wait: the game creates its D3D device right after SteamStub finishes unpacking.
    if (config.overlay && !debug_overlay::install()) logger::write("adapter: overlay unavailable");
    if (!waitForDecryption()) {
        logger::write("adapter: game code not decrypted after %lu ms, staying inert", kDecryptTimeoutMs);
        return;
    }
    if (config.netTrace) net_trace::enable();
    if (config.autoJoin) auto_join::enable();
    if (config.coop) save_redirect::install(reportCloudWrite);
    if (config.coop) enableCoop();
    if (config.trace) vtable_tracer::install(config.traceVtables);
    game_tick::install();
    crash_dump::install();
    if (config.coop) input_redirect::install();
    state_sync::start(g_net, config.port);
}

void logFatal(DWORD code) { logger::write("adapter: unhandled exception 0x%08lx during init, staying inert", code); }

}  // namespace

DWORD WINAPI initThread(LPVOID) {
    __try {
        startSubsystems();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logFatal(GetExceptionCode());
    }
    return 0;
}

void shutdownAdapter() {
    debug_overlay::uninstall();
    save_redirect::uninstall();
    input_redirect::uninstall();
    pad_commands::uninstall();
    player_damage::uninstall();
    menu_mirror::uninstall();
    floor_items_sync::uninstall();
    door_sync::uninstall();
    event_sync::uninstall();
    session_slot::uninstall();
    pickup_guard::uninstall();
    game_tick::uninstall();
    vtable_tracer::uninstall();
}
