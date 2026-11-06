// Checks the GAME_STATE report: the place from the room phase and the text. Exit 0 when every check passes.
#include <cstdio>

#include "../src/game_state_report_rule.h"

namespace {

using namespace game_state_report_rule;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testPlace() {
    check(placeOf(room_phase::Init, false, false) == Place::Title, "boot is the title");
    check(placeOf(room_phase::Opening, false, false) == Place::Title, "the opening is the title");
    check(placeOf(room_phase::Dead, false, false) == Place::Title, "game over is the title");
    check(placeOf(room_phase::kUnreadable, false, false) == Place::Title, "an unreadable phase is the title");
    check(placeOf(room_phase::Main, false, false) == Place::World, "the main phase is the world");
    check(placeOf(room_phase::SubScreen, false, false) == Place::World, "a menu is the world");
    check(placeOf(room_phase::Main, true, false) == Place::Loading, "a running door is loading");
    check(placeOf(room_phase::DoorLoad, false, false) == Place::Loading, "the door phase is loading");
    check(placeOf(room_phase::Init, false, true) == Place::Loading, "the session's load from the title is loading");
}

void testFormat() {
    Report report;
    check(format(report) == "place=title;char=off;save=off;slot=-1;coop=0;load=none", "the default report");
    report.place = Place::World;
    report.save = Save::Ok;
    report.slot = 4;
    report.coop = true;
    report.load = Load::Failed;
    check(format(report) == "place=world;char=off;save=ok;slot=4;coop=1;load=failed", "a report in the world after a failed load");
    report.place = Place::Loading;
    report.save = Save::Refused;
    report.load = Load::Ok;
    check(format(report) == "place=loading;char=off;save=refused;slot=4;coop=1;load=ok", "loading with a refused save");
}

}  // namespace

int main() {
    testPlace();
    testFormat();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
