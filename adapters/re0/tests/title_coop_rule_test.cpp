// Checks the Co-op entry rules of the title screen: where the fifth row's choice sends the title and what place the
// lobby is told (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/title_coop_rule.h"

namespace {

using namespace title_coop_rule;
using lobby_wire::Place;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testChoice() {
    check(afterCoopChoice(true) == kStateOptionsFadeOut, "the Co-op row goes down the Options row's path when the page will show");
    // The guard this entry keeps: without the page the row never reaches the store state's handler.
    check(afterCoopChoice(false) == kStateMainMenuOpen, "without the page the main menu comes back, never the store");
    check(afterCoopChoice(false) != kStateStoreChosen && afterCoopChoice(true) != kStateStoreChosen, "the store state is always left");
}

void testParkedState() {
    check(kStateParked > kLastGameState, "the parked state is outside the title's dispatch");
    check(kStateStoreChosen <= kLastGameState && kStateMainMenuOpen <= kLastGameState && kStateOptionsFadeOut <= kLastGameState,
          "the game's states are inside");
}

void testPlace() {
    check(placeOf(false, false) == Place::Title, "the title's own menus");
    check(placeOf(true, false) == Place::Page, "the Co-op page");
    check(placeOf(true, true) == Place::Loading && placeOf(false, true) == Place::Loading, "a start in progress is loading");
}

}  // namespace

int main() {
    testChoice();
    testParkedState();
    testPlace();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
