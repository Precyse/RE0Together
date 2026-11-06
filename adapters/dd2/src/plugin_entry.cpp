// REFramework plugin entry (reframework\plugins\dd2coop.dll): checks the build, installs the hook sets, registers the
// game-thread tick and links to the launcher. Built so far: the world gate, the session messages, the owner body stream and peer
// reports, the shared world clock, forced offline play, the guest's save redirect and co-op profile, and peer puppets (DD2
// design phases 1 to 4).
#include <windows.h>

#include <cstring>
#include <string>

#include "build_gate.h"
#include "calls.h"
#include "config.h"
#include "crash_dump.h"
#include "echo_puppet.h"
#include "peer_puppet.h"
#include "plague_off.h"
#include "profile.h"
#include "roster.h"
#include "enemy_mirror.h"
#include "enemy_spawn.h"
#include "enemy_stream.h"
#include "puppet_brain.h"
#include "puppet_driver.h"
#include "fnv1a32.h"
#include "load_slot.h"
#include "log.h"
#include "names.h"
#include "refw.h"
#include "offline_capture.h"
#include "overlay.h"
#include "save_folders.h"
#include "save_redirect.h"
#include "session.h"
#include "tick.h"
#include "wire_dd2.h"
#include "world_clock.h"
#include "world_gate.h"

namespace {

// REFramework's target name for the game (sdk::GameIdentity, compared exactly by its plugin loader; not the "dd2" it logs).
constexpr const char* kReframeworkGameName = "DD2";

uint32_t namesHash() {
    uint32_t state = fnv1a32::kOffsetBasis;
    auto add = [&state](const char* type, const char* method, const char* params, uintptr_t pinned) {
        const std::string text = std::string(type) + "." + method + "(" + params + ")@" + std::to_string(pinned);
        state = fnv1a32::hash({reinterpret_cast<const uint8_t*>(text.data()), text.size()}, state);
    };
    for (const names::MethodRow& row : names::kAll) add(row.type, row.method, row.params, row.pinned);
    for (const calls::Row& row : calls::kRows) add(row.type, row.method, row.params, row.pinned);
    return state;
}

}  // namespace

extern "C" __declspec(dllexport) void reframework_plugin_required_version(REFrameworkPluginVersion* version) {
    version->major = REFRAMEWORK_PLUGIN_VERSION_MAJOR;
    version->minor = REFRAMEWORK_PLUGIN_VERSION_MINOR;
    version->patch = REFRAMEWORK_PLUGIN_VERSION_PATCH;
    version->game_name = kReframeworkGameName;
}

extern "C" __declspec(dllexport) bool reframework_plugin_initialize(const REFrameworkPluginInitializeParam* param) {
    refw::init(param);
    logger::write("dd2coop: start");
    crash_dump::install();
    const Config config = loadConfig();
    if (!build_gate::checkRunningGame()) return true;  // inert: loaded, nothing hooked

    uint16_t modules = 0;
    if (world_gate::install()) modules |= wire_dd2::kModuleWorldGate;
    calls::resolve();
    // A co-op guest plays the host's world as the character it picked; a host's campaign is its own world; see save_folders.h.
    // Every other launch keeps its profile fresh.
    const bool carried = config.coop && config.guest;
    const save_folders::Folders folders = save_folders::of(config);
    if (folders.mount) save_redirect::enable(*folders.mount);
    if (profile::install(carried ? profile::Mode::Splice : profile::Mode::Capture, folders.profile)) modules |= wire_dd2::kModuleProfile;
    if (config.captureSlot >= 0 && !carried) offline_capture::enable(config.captureSlot);
    load_slot::install();
    if (config.coop) overlay::install();
    if (config.coop && plague_off::install()) modules |= wire_dd2::kModulePlagueOff;
    const bool roster = config.coop && roster::install();
    if (roster) modules |= wire_dd2::kModuleRoster;
    world_clock::install();
    echo_puppet::setEnabled(config.echoPuppet);
    // Peer puppets and enemy mirrors are driven only with the brain-off hooks in and the driver's literals found.
    const bool enemySync = config.enemySync && roster;
    const bool brain = (config.peerPuppet || enemySync) && calls::enabled(calls::Feature::Puppet) && puppet_brain::install() &&
                       puppet_driver::init();
    const bool puppets = config.peerPuppet && brain;
    peer_puppet::setEnabled(puppets);
    if (config.peerPuppet && !puppets) logger::write("dd2coop: peer puppets off (puppet calls, hooks or literals missing)");
    const bool enemies = enemySync && brain && calls::enabled(calls::Feature::Enemy) && puppet_brain::installMirror() &&
                         enemy_spawn::init();
    enemy_stream::setEnabled(enemies);
    enemy_mirror::setEnabled(enemies);
    if (config.enemySync && !enemies) logger::write("dd2coop: enemy sync off (co-op, roster, enemy calls, hooks or literals missing)");
    if (calls::enabled(calls::Feature::Body)) modules |= wire_dd2::kModuleBody;
    if (calls::enabled(calls::Feature::Clock)) modules |= wire_dd2::kModuleClock;
    if (calls::enabled(calls::Feature::Offline)) modules |= wire_dd2::kModuleOffline;
    if (puppets) modules |= wire_dd2::kModulePuppet;
    if (enemies) modules |= wire_dd2::kModuleEnemySync;
    const wire_dd2::GameHello hello{build_gate::kSupportedTimeDateStamp, refw::tdbHash(), namesHash(), wire_dd2::kProto, modules};
    if (config.captureSlot < 0) session::start(config.port, hello);  // a capture launch is not the session's game
    const bool ticking = tick::install();
    logger::write("dd2coop: %s, modules 0x%X", ticking ? "on" : "tick unavailable, off", modules);
    return true;
}
