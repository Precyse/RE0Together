// Checks the save routing and automatic join rules (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/session_rule.h"

namespace {

using session_rule::JoinMode;
using session_rule::SaveRoute;
using session_rule::joinMode;
using session_rule::saveRoute;
namespace phase = room_phase;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testSaveRoute() {
    check(saveRoute(true, phase::Save, true, true) == SaveRoute::Refuse, "guest save refused");
    check(saveRoute(true, phase::Save, true, false) == SaveRoute::Refuse, "guest save refused after the host left");
    check(saveRoute(true, phase::Save, false, true) == SaveRoute::CoopSlot, "host save goes to the co-op slot");
    check(saveRoute(true, phase::Save, false, false) == SaveRoute::Pass, "solo save untouched");
    check(saveRoute(true, phase::Init, true, true) == SaveRoute::Pass, "boot-time save passes");
    check(saveRoute(false, phase::Save, true, true) == SaveRoute::Pass, "system slot passes");
}

void testJoinMode() {
    check(joinMode(false, true, phase::Dead, false, true, false) == JoinMode::Idle, "host never joins");
    check(joinMode(true, false, phase::Dead, true, true, false) == JoinMode::Confirming, "game over follows the host without full join");
    check(joinMode(true, false, phase::Dead, true, false, false) == JoinMode::Waiting, "game over waits for the host");
    check(joinMode(true, false, phase::Main, false, true, false) == JoinMode::Idle, "title untouched without full join");
    check(joinMode(true, true, phase::Init, false, true, false) == JoinMode::Confirming, "full join confirms the title");
    check(joinMode(true, true, phase::Init, false, false, false) == JoinMode::Waiting, "full join waits for the host");
    check(joinMode(true, true, phase::Main, true, true, false) == JoinMode::Idle, "in game is idle");
    // The guard the Co-op page's start adds: a title that follows the host's starts is never pressed through.
    check(joinMode(true, true, phase::Init, false, true, true) == JoinMode::Idle, "a title following the host's start is not pressed");
    check(joinMode(true, true, phase::Dead, true, true, true) == JoinMode::Confirming, "game over still follows the host");
}

}  // namespace

int main() {
    testSaveRoute();
    testJoinMode();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
