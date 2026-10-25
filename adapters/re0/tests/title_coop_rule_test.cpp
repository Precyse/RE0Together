// Checks the Co-op entry rules of the title screen: when the page opens, stays, closes or is dropped, and what closes
// it (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/title_coop_rule.h"

namespace {

using namespace title_coop_rule;

constexpr uint32_t kConfirm = 0x1000;
constexpr uint32_t kCancel = 0x2000;
constexpr uint32_t kUp = 0x10;
constexpr uint32_t kMainMenuInput = 0x20;
constexpr uint32_t kOptionsFadeOut = 0x26;  // the Options row's choice
constexpr uint32_t kPressStart = 6;         // where cancel on the main menu goes

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testOpen() {
    check(step(kStateStoreChosen, false, false) == Step::OpenPage, "the fifth row's choice opens the page");
    check(step(kMainMenuInput, false, false) == Step::None, "the menu waiting for input is left alone");
    check(step(kStateMainMenuOpen, false, false) == Step::None, "the menu opening is left alone");
    check(step(kOptionsFadeOut, false, false) == Step::None, "another row's choice is the game's");
    check(step(kStateParked, false, false) == Step::None, "a parked state without our page is not ours to open");
}

void testWhileOpen() {
    check(step(kStateParked, true, false) == Step::KeepPage, "the page stays while the title is parked");
    check(step(kStateParked, true, true) == Step::ClosePage, "confirm or cancel closes the page");
    // The guard this entry adds: the game moved the title (returned to the press-start screen, rebuilt it); the page
    // goes without writing the title's state, and never opens the store state a second time.
    check(step(kPressStart, true, false) == Step::DropPage, "the game moved the title: the page is dropped");
    check(step(kPressStart, true, true) == Step::DropPage, "a press on a moved title does not reopen the menu");
    check(step(kStateStoreChosen, true, false) == Step::DropPage, "the store state with the page open drops it");
}

void testParkedState() {
    check(kStateParked > kLastGameState, "the parked state is outside the title's dispatch");
    check(kStateStoreChosen <= kLastGameState && kStateMainMenuOpen <= kLastGameState, "the game's states are inside");
}

void testClose() {
    check(closesPage(kConfirm, kConfirm, kCancel), "confirm closes the page");
    check(closesPage(kCancel, kConfirm, kCancel), "cancel closes the page");
    check(closesPage(kUp | kCancel, kConfirm, kCancel), "cancel with another bit closes the page");
    check(!closesPage(kUp, kConfirm, kCancel), "a direction keeps the page open");
    check(!closesPage(0, kConfirm, kCancel), "no press keeps the page open");
    check(!closesPage(kConfirm, 0, 0), "no masks (no pad object) keep the page open");
}

}  // namespace

int main() {
    testOpen();
    testWhileOpen();
    testParkedState();
    testClose();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
