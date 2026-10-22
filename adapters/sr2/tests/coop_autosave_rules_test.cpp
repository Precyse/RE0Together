// The guest's free-roam autosave rules (no game): 5 minutes after the last save, only at a safe moment, a game save restarts it.
#include "../src/coop_autosave_rules.h"
#include "check.h"

using coop_autosave_rules::kFreeRoam;
using coop_autosave_rules::kIntervalMs;
using coop_autosave_rules::Timer;
using coop_autosave_rules::World;

namespace {

World freeRoam() {
    return {.guest = true, .inGame = true, .saving = false, .missionType = kFreeRoam, .held = false, .cutscene = false,
            .paused = false, .playerUp = true, .deviceDialog = false};
}

}  // namespace

int main() {
    Timer timer;
    const uint64_t start = 1000;
    check(!timer.due(start, freeRoam()), "the first tick in game starts the clock");
    check(!timer.due(start + kIntervalMs - 1, freeRoam()), "nothing before 5 minutes");
    check(timer.due(start + kIntervalMs, freeRoam()), "5 minutes of free roam since the last save asks for an autosave");
    check(!timer.due(start + kIntervalMs + 1, freeRoam()), "and starts the clock again");

    Timer host;
    World hosting = freeRoam();
    hosting.guest = false;
    host.due(start, hosting);
    check(!host.due(start + 2 * kIntervalMs, hosting), "the host is never autosaved: its game dying ends the session");

    Timer mission;
    mission.due(start, freeRoam());
    World inMission = freeRoam();
    inMission.missionType = 0;
    check(!mission.due(start + kIntervalMs, inMission), "never in a mission: the save would write the mission's start");
    World down = freeRoam();
    down.playerUp = false;
    check(!mission.due(start + kIntervalMs + 1, down), "never while the player is down");
    World cutscene = freeRoam();
    cutscene.cutscene = true;
    check(!mission.due(start + kIntervalMs + 2, cutscene), "never in a cutscene");
    check(mission.due(start + kIntervalMs + 3, freeRoam()), "the first safe moment after that saves");

    Timer saved;
    saved.due(start, freeRoam());
    World saving = freeRoam();
    saving.saving = true;
    saved.due(start + kIntervalMs - 10, saving);
    check(!saved.due(start + kIntervalMs, freeRoam()), "a save the game wrote itself starts the clock again");

    check(coop_autosave_rules::deviceDialog(0, false, true), "no save device and no saves: the game's dialog, not a save");
    check(coop_autosave_rules::deviceDialog(0x200, false, false), "more than 0x1FF saves without an autosave: the dialog");
    check(!coop_autosave_rules::deviceDialog(0x200, true, false), "an autosave listed is overwritten whatever the count");
    check(!coop_autosave_rules::deviceDialog(3, false, false), "a few saves and a device: the save is written");
    return g_failures == 0 ? 0 : 1;
}
