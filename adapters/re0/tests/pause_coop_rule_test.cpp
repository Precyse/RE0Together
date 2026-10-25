// Checks the pause menu's three rows (no game): the layout, cursor wrap, clicks and what decide does on each row. Exit 0
// when every check passes.
#include <cstdio>

#include "../src/pause_coop_rule.h"

namespace {

namespace rule = pause_coop_rule;
using rule::Action;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

rule::Input with(bool cancel, bool decide, bool up, bool down, int clicked = -1) { return {cancel, decide, up, down, clicked}; }

void testLayout() {
    check(rule::rowY(rule::kBack) == 400.0f && rule::rowY(rule::kCoop) == 440.0f && rule::rowY(rule::kQuit) == 480.0f,
          "Back, Co-op and Quit Game sit 40 apart from the layout's first row");
    check(rule::kHelpY > rule::rowY(rule::kQuit), "the help line sits under the last row");
}

void testSteps() {
    check(rule::step(rule::kBack, with(false, false, false, true)).row == rule::kCoop, "down from Back is Co-op");
    check(rule::step(rule::kQuit, with(false, false, false, true)).row == rule::kBack, "down from Quit Game wraps to Back");
    check(rule::step(rule::kBack, with(false, false, true, false)).row == rule::kQuit, "up from Back wraps to Quit Game");
    check(rule::step(rule::kCoop, with(false, false, true, false)).action == Action::Move, "a move is a move");
    check(rule::step(rule::kBack, with(false, true, false, false)).action == Action::Back, "decide on Back resumes");
    check(rule::step(rule::kCoop, with(false, true, false, false)).action == Action::Coop, "decide on Co-op opens the page");
    check(rule::step(rule::kQuit, with(false, true, false, false)).action == Action::Quit, "decide on Quit Game asks to quit");
    check(rule::step(rule::kCoop, with(true, true, false, false)).action == Action::Cancel, "cancel wins over decide");
    check(rule::step(rule::kBack, with(false, false, false, false)).action == Action::None, "no input, nothing");
    const rule::Step moved = rule::step(rule::kBack, with(false, false, false, false, rule::kQuit));
    check(moved.action == Action::Move && moved.row == rule::kQuit, "a click on another row moves there");
    check(rule::step(rule::kCoop, with(false, false, false, false, rule::kCoop)).action == Action::Coop, "a click on the cursor row decides");
}

}  // namespace

int main() {
    testLayout();
    testSteps();
    if (g_failures == 0) std::printf("pause_coop_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
