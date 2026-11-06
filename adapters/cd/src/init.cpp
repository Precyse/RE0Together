#include "init.h"

#include "build_gate.h"
#include "cd_sites.h"
#include "config.h"
#include "containment.h"
#include "crash_dump.h"
#include "equipment.h"
#include "funnels.h"
#include "game_state.h"
#include "hooks.h"
#include "link.h"
#include "log.h"
#include "main_tick.h"
#include "minimap.h"
#include "presence_recv.h"
#include "presence_send.h"
#include "safe_read.h"
#include "troc_catalogue.h"
#include "wire_cd.h"
#include "world_gate.h"
#include "save_guard.h"
#include "stage_guard.h"
#include "story.h"
#include "world_time.h"

namespace {

constexpr DWORD kWorldSystemPollMs = 500;

// Nothing is read or hooked before the title screen's WorldSystem exists: by then Denuvo has run and the Troc
// instances hold their derived vtables.
void waitForWorldSystem() {
    if (!safe_read::pointer(cd_sites::kWorldSystem)) logger::write("adapter: waiting for the title (WorldSystem)");
    while (!safe_read::pointer(cd_sites::kWorldSystem)) Sleep(kWorldSystemPollMs);
    logger::write("adapter: WorldSystem present");
}

}  // namespace

DWORD WINAPI initThread(LPVOID) {
    logger::write("adapter: start (Crimson Desert)");
    crash_dump::install();
    const Config config = loadConfig();
    if (!build_gate::checkRunningGame() || !build_gate::checkNoConflict()) return 0;
    waitForWorldSystem();
    crash_dump::install();  // the game's own reporter (sentry) may have put its filter in front of ours by now

    const bool catalogueOk = troc_catalogue::build();
    world_gate::watchOpcodes();
    const bool presence = presence_send::watchOpcodes() && presence_recv::resolveOpcodes();
    presence_recv::setEnabled(presence && config.presence);
    game_state::setPresence(config.presence, presence);
    logger::write("adapter: presence %s", !presence ? "off (message names missing)" : config.presence ? "on" : "off (adapter.ini presence=0)");
    const bool equipmentNames = presence && equipment::resolveOpcodes();
    const bool minimapNames = presence && minimap::resolveOpcodes();
    if (!world_time::resolveOpcodes()) logger::write("adapter: world time off (message names missing)");
    const bool storyNames = story::resolveOpcodes();
    if (!storyNames) logger::write("adapter: story off (message names missing)");

    hooks::beginBatch();
    uint32_t sets = 0;
    if (funnels::install(config.capture)) sets |= wire_cd::kSetFunnels;
    if (main_tick::install(config.devCommands)) sets |= wire_cd::kSetTick;
    if (equipmentNames && equipment::install()) sets |= wire_cd::kSetEquipment;
    if (minimapNames && minimap::install()) sets |= wire_cd::kSetMinimap;
    if (containment::install()) sets |= wire_cd::kSetContainment;
    if (storyNames && stage_guard::install()) sets |= wire_cd::kSetStory;
    const bool replay = config.storyReplay && (sets & wire_cd::kSetStory);
    story::setReplay(replay);
    if (!save_guard::resolveOpcodes(replay)) logger::write("adapter: save guard off (SaveAutoReq missing)");
    link::start(config.port, sets, catalogueOk);  // before the tick hook goes live, which polls it
    hooks::applyBatch();
    containment::confirmEnabled();

    logger::write("adapter: sets 0x%X, catalogue %s, %s", sets, catalogueOk ? "ok" : "catalogue_fail",
                  sets == (wire_cd::kSetFunnels | wire_cd::kSetTick | wire_cd::kSetEquipment | wire_cd::kSetMinimap | wire_cd::kSetContainment) ? "installed" : "partly installed");
    return 0;
}
