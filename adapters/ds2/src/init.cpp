#include "init.h"

#include "cargo_menu.h"
#include "config.h"
#include "crash_dump.h"
#include "documents_redirect.h"
#include "dx12_hook.h"
#include "game.h"
#include "input_filter.h"
#include "log.h"
#include "main_thread.h"
#include "marker_overlay.h"
#include "net_client.h"
#include "player_sync.h"
#include "ds2/remote_animation.h"
#include "ds2/combat_hook.h"
#include "ds2/health_watch.h"
#include "ds2/npc_damage_probe.h"
#include "ds2/camp_alert.h"
#include "ds2/cargo_defer.h"
#include "ds2/cutscene_log.h"
#include "ds2/enemy_host.h"
#include "ds2/enemy_puppet.h"
#include "ds2/damage_veto.h"
#include "ds2/enemy_spawn.h"
#include "ds2/local_weapon.h"
#include "ds2/remote_weapon.h"
#include "ds2/sim_tick.h"
#include "ds2/partner_cargo.h"
#include "ds2/story.h"
#include "ds2/test_commands.h"
#include "ds2/warp.h"
#include "ds2/world_pause.h"
#include "ds2/structures.h"
#include "ds2/bt_events.h"
#include "ds2/world_env.h"
#include "ds2/world_facts.h"
#include "remote_body.h"
#include "vehicle_sync.h"
#include "weapon_sync.h"

namespace {

constexpr DWORD kResolvePollMs = 1000;

// Leaked on purpose: joining the net thread from a static destructor would run under the loader lock.
NetClient& g_net = *new NetClient;

// Engine work on the game's simulation thread: the vehicles partners drive (their bodies run on the engine's own update).
void simulationTick() {
    vehicle_sync::place();
}

void drawOverlay(float width, float height) {
    marker_overlay::draw(width, height);
    cargo_menu::draw(width, height);
    warp::poll();
}

}  // namespace

DWORD WINAPI initThread(LPVOID) {
    logger::write("adapter: start (DEATH STRANDING 2)");
    logger::write("adapter: saves go to the session folder: %s", documents_redirect::active() ? "yes" : "no");
    crash_dump::install();
    const Config config = loadConfig();
    marker_overlay::setSelfMarker(config.selfMarker);
    remote_body::setEnabled(config.remoteBody);
    if (config.remoteBody) remote_body::installEarly();
    remote_animation::setMirrorLocalPlayer(config.mirrorAnimation);
    if (config.overlay && !dx12_hook::install(drawOverlay)) logger::write("adapter: overlay unavailable");
    input_filter::install(cargo_menu::claimsKey);
    game::watchOrders();
    world_facts::installEarly(config.logFacts);
    world_env::installEarly();
    bt_events::installEarly();
    if (config.enemySync) {
        enemy_spawn::installEarly();
        enemy_host::installEarly();
        camp_alert::installEarly();
        enemy_puppet::installEarly();
        combat_hook::installEarly();
        npc_damage_probe::installEarly();
        health_watch::installEarly();
    }
    weapon_sync::setEnabled(config.weaponSync);
    if (config.weaponSync) {
        local_weapon::installEarly();
        remote_weapon::installEarly(config.weaponAttachMode);
        damage_veto::installEarly();
    }
    story::installEarly();
    warp::installEarly();
    cargo_defer::installEarly();
    world_pause::installEarly();
    if (config.testCommands) test_commands::installEarly();
    if (config.cutsceneLog) cutscene_log::installEarly();
    partner_cargo::installEarly();
    structures::installEarly();
    sim_tick::installEarly();
    if (!game::watchInteractions()) logger::write("adapter: interaction watch unavailable, guests are not restricted");
    while (!game::resolve()) Sleep(kResolvePollMs);
    logger::write("adapter: engine objects found, linking to the launcher on port %u", config.port);
    if (!main_thread::install(game::frameFunction(), simulationTick)) {
        logger::write("adapter: no simulation-thread hook, partners' vehicles stay put");
    }
    player_sync::start(g_net, config.port);
    return 0;
}
