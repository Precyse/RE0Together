#include "init.h"

#include "build_guard.h"
#include "cargo_menu.h"
#include "config.h"
#include "crash_dump.h"
#include "documents_redirect.h"
#include "dx12_hook.h"
#include "game.h"
#include "hooks.h"
#include "input_filter.h"
#include "log.h"
#include "marker_overlay.h"
#include "net_client.h"
#include "peer_pose.h"
#include "player_sync.h"
#include "order_handoff.h"
#include "rack_sync.h"
#include "toast_queue.h"
#include "ds2/remote_animation.h"
#include "ds2/combat_hook.h"
#include "ds2/damage_diag.h"
#include "ds2/ride_probe.h"
#include "ds2/health_watch.h"
#include "ds2/orders_diag.h"
#include "ds2/camp_alert.h"
#include "ds2/cargo_defer.h"
#include "ds2/player_system_guard.h"
#include "equip_sync.h"
#include "gear_restore.h"
#include "ds2/loading_screen.h"
#include "ds2/cutscene.h"
#include "ds2/cutscene_log.h"
#include "ds2/enemy_host.h"
#include "ds2/enemy_puppet.h"
#include "ds2/damage_veto.h"
#include "ds2/enemy_spawn.h"
#include "ds2/enemy_vitals.h"
#include "ds2/local_weapon.h"
#include "ds2/remote_weapon.h"
#include "ds2/sim_tick.h"
#include "ds2/partner_cargo.h"
#include "ds2/partner_focus.h"
#include "ds2/sneaking_focus.h"
#include "ds2/story.h"
#include "ds2/test_commands.h"
#include "ds2/warp.h"
#include "ds2/world_pause.h"
#include "ds2/structures.h"
#include "ds2/bt_diag.h"
#include "ds2/bt_events.h"
#include "ds2/bt_grab.h"
#include "ds2/craters.h"
#include "ds2/lockers.h"
#include "ds2/user_markers.h"
#include "ds2/world_env.h"
#include "ds2/world_facts.h"
#include "remote_body.h"
#include "vehicle_sync.h"
#include "weapon_sync.h"

namespace {

constexpr DWORD kResolvePollMs = 1000;
constexpr float kBuildToastSeconds = 600.0f;

// Leaked on purpose: joining the net thread from a static destructor would run under the loader lock.
NetClient& g_net = *new NetClient;

void drawOverlay(float width, float height) {
    marker_overlay::draw(width, height);
    cargo_menu::draw(width, height);
    warp::poll();
}

// The overlay of an unsupported build: toasts only, nothing that reads the game.
void drawNoticeOnly(float width, float) { marker_overlay::drawToasts(width); }

}  // namespace

DWORD WINAPI initThread(LPVOID) {
    logger::write("adapter: start (DEATH STRANDING 2)");
    logger::write("adapter: saves go to the session folder: %s", documents_redirect::active() ? "yes" : "no");
    crash_dump::install();
    const Config config = loadConfig();
    const bool supportedBuild = build_guard::checkRunningGame();
    if (config.overlay && !dx12_hook::install(supportedBuild ? drawOverlay : drawNoticeOnly)) {
        logger::write("adapter: overlay unavailable");
    }
    if (!supportedBuild) {
        toast_queue::push("Unsupported game version: co-op is off", kBuildToastSeconds);
        return 0;
    }
    hooks::beginBatch();
    marker_overlay::setSelfMarker(config.selfMarker);
    remote_body::setEnabled(config.remoteBody);
    peer_pose::installEarly();
    if (config.remoteBody) remote_body::installEarly();
    remote_animation::setMirrorLocalPlayer(config.mirrorAnimation);
    input_filter::install(cargo_menu::claimsKey);
    game::watchOrders();
    world_facts::installEarly(config.logFacts);
    world_env::installEarly();
    bt_events::installEarly();
    craters::installEarly();
    if (config.diagnostics) bt_diag::installEarly();
    if (config.enemySync) {
        enemy_vitals::installEarly();
        enemy_spawn::installEarly();
        enemy_host::installEarly(config.diagnostics);
        camp_alert::installEarly();
        enemy_puppet::installEarly();
        bt_grab::installEarly(config.diagnostics);
        combat_hook::installEarly(config.diagnostics);
        combat_hook::setGodMode(config.godMode && config.testCommands);
        if (config.diagnostics) damage_diag::installEarly();
        health_watch::installEarly();
    }
    weapon_sync::setEnabled(config.weaponSync);
    if (config.weaponSync) {
        local_weapon::installEarly();
        remote_weapon::installEarly(config.weaponAttachMode, config.diagnostics);
        damage_veto::installEarly();
    }
    story::installEarly();
    warp::installEarly();
    cargo_defer::installEarly();
    equip_sync::installEarly();
    player_system_guard::installEarly();
    gear_restore::setEnabled(config.gearRestore);
    gear_restore::installEarly();
    if (config.ordersDiagnostics) orders_diag::installEarly();
    world_pause::installEarly();
    loading_screen::installEarly();
    if (config.testCommands) test_commands::installEarly();
    if (config.cutsceneLog) cutscene_log::enable();
    if (config.cutsceneLog || config.cutsceneSync) cutscene::installEarly(config.cutsceneSync, config.testCommands ? config.cutsceneShareMinFrames : 0);
    partner_cargo::installEarly(config.diagnostics);
    lockers::installEarly();
    user_markers::installEarly(config.diagnostics);
    partner_focus::installEarly(config.diagnostics);
    sneaking_focus::installEarly(config.diagnostics);
    structures::installEarly();
    vehicle_sync::installEarly();
    if (config.diagnostics) ride_probe::installEarly();
    rack_sync::installEarly();
    order_handoff::installEarly();
    sim_tick::installEarly();
    if (!game::watchInteractions()) logger::write("adapter: interaction watch unavailable, guests are not restricted");
    hooks::applyBatch();
    while (!game::resolve()) Sleep(kResolvePollMs);
    logger::write("adapter: engine objects found, linking to the launcher on port %u", config.port);
    player_sync::start(g_net, config.port);
    return 0;
}
