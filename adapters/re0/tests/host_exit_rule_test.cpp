// Checks when a guest decides its host's game has left play, and when its own game is already leaving (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/host_exit_rule.h"

namespace {

namespace rule = host_exit_rule;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testAway() {
    rule::AwayWatch watch;
    check(!watch.hostLeftGame(true, 0), "a host in play is fine");
    check(!watch.hostLeftGame(false, 1000), "the host just left play: wait");
    check(!watch.hostLeftGame(false, 1000 + rule::kHostAwayMs - 1), "not yet");
    check(watch.hostLeftGame(false, 1000 + rule::kHostAwayMs), "out of play long enough: leave");
    check(!watch.hostLeftGame(false, 1000 + 2 * rule::kHostAwayMs), "said once");
    rule::AwayWatch blip;
    check(!blip.hostLeftGame(false, 0), "a blip starts the wait");
    check(!blip.hostLeftGame(true, 2000), "the host is back: the wait ends");
    check(!blip.hostLeftGame(false, rule::kHostAwayMs + 100), "a new absence starts over");
    watch.clear();
    check(!watch.hostLeftGame(false, 50000), "cleared while the world leaves: starts over");
}

void testLeavingOnItsOwn() {
    check(rule::leavingOnItsOwn(room_phase::Dead), "the game over leaves the world: a request is spent");
    check(rule::leavingOnItsOwn(room_phase::Exit), "the exit to the title: a request is spent");
    check(!rule::leavingOnItsOwn(room_phase::Main), "plain gameplay keeps a request");
    check(!rule::leavingOnItsOwn(room_phase::Init), "a world loading in keeps a request");
    check(!rule::leavingOnItsOwn(room_phase::DoorLoad), "a door keeps a request");
    check(!rule::leavingOnItsOwn(room_phase::kUnreadable), "an unreadable phase keeps a request");
}

}  // namespace

int main() {
    testAway();
    testLeavingOnItsOwn();
    if (g_failures == 0) std::printf("host_exit_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
