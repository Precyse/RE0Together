// Checks the config.ini key parsing and the control truth table (no game). Exit 0 when every check passes.
#include <windows.h>

#include <cstdio>

#include "../src/control_rule.h"
#include "../src/key_config.h"

namespace {

using control_rule::byOwnership;
using control_rule::byPresence;
using control_rule::PeerPlace;
using control_rule::runsEnemies;
using control_rule::Control;
using control_rule::kNoOwner;

constexpr int kLocalSlot = 1;
constexpr int kPeerSlot = 2;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testKeyNames() {
    check(key_config::virtualKeyOf("KB_V") == 'V', "letter");
    check(key_config::virtualKeyOf("KB_7") == '7', "digit");
    check(key_config::virtualKeyOf("KB_SPACE") == VK_SPACE, "space");
    check(key_config::virtualKeyOf("KB_SHIFT") == VK_SHIFT, "shift");
    check(key_config::virtualKeyOf("KB_TAB") == VK_TAB, "tab");
    check(key_config::virtualKeyOf("KB_UP") == VK_UP, "up");
    check(key_config::virtualKeyOf("KB_DOWN") == VK_DOWN, "down");
    check(key_config::virtualKeyOf("KB_LEFT") == VK_LEFT, "left");
    check(key_config::virtualKeyOf("KB_RIGHT") == VK_RIGHT, "right");
    check(key_config::virtualKeyOf("KB_F5") == 0, "unsupported name");
    check(key_config::virtualKeyOf("V") == 0, "missing prefix");
    check(key_config::virtualKeyOf("KB_a") == 0, "lowercase");
}

void testParse() {
    const auto keys = key_config::parse("KC_front=KB_W\r\nKC_trace = KB_TAB\r\nKC_change=KB_UP\r\nKC_reload=KB_R\r\n");
    check(keys.change == VK_UP && keys.trace == VK_TAB, "custom bindings with CRLF and spaces");
    const auto defaults = key_config::parse("KC_front=KB_W\n");
    check(defaults.change == 'V' && defaults.trace == 'E', "missing keys default to V and E");
    const auto unknown = key_config::parse("KC_change=KB_F9\nKC_trace=garbage\n");
    check(unknown.change == 'V' && unknown.trace == 'E', "unknown values default");
    const auto last = key_config::parse("KC_change=KB_Z");
    check(last.change == 'Z', "last line without newline");
}

void testRealConfigFormat() {
    const auto keys = key_config::parse(
        "[JOYPAD]\r\nKC_front=KB_W\r\nKC_change=KB_V\r\nKC_trace=KB_E\r\nKC_reload=KB_R\r\n[KEYBOARD]\r\n");
    check(keys.change == 'V' && keys.trace == 'E', "[JOYPAD] section, KC_change=KB_V and KC_trace=KB_E");
    const auto rebound = key_config::parse("[JOYPAD]\nKC_change=PAD_Y\n[KEYBOARD]\nKC_change=KB_Q\nKC_trace=KB_E\n");
    check(rebound.change == 'Q' && rebound.trace == 'E', "non-keyboard binding skipped for a later KB_ line");
}

void testOwnership() {
    check(byOwnership(false, true, kLocalSlot, kLocalSlot) == Control::Vanilla, "no peer, host: vanilla");
    check(byOwnership(false, false, kPeerSlot, kLocalSlot) == Control::Vanilla, "no peer, guest: vanilla");
    check(byOwnership(true, false, kNoOwner, kLocalSlot) == Control::Locked, "guest without owner: locked");
    check(byOwnership(true, true, kNoOwner, kLocalSlot) == Control::Vanilla, "host without owner: vanilla");
    check(byOwnership(true, true, kLocalSlot, kLocalSlot) == Control::Local, "own character: local");
    check(byOwnership(true, false, kPeerSlot, kLocalSlot) == Control::Remote, "peer's character: remote");
}

void testPresence() {
    check(byPresence(Control::Remote, true) == Control::Remote, "remote owner in this room: remote");
    check(byPresence(Control::Remote, false) == Control::Locked, "remote owner elsewhere: locked");
    for (const bool here : {true, false}) {
        check(byPresence(Control::Local, here) == Control::Local, "local is never locked by presence");
        check(byPresence(Control::Vanilla, here) == Control::Vanilla, "vanilla is never changed");
        check(byPresence(Control::Locked, here) == Control::Locked, "locked stays locked");
    }
}

void testEnemyAuthority() {
    for (const bool host : {true, false}) {
        check(runsEnemies(PeerPlace::Elsewhere, host, false, false), "alone in the room: runs its enemies");
        check(runsEnemies(PeerPlace::Unknown, host, true, false) == host, "peer room unknown: the host runs them");
    }
    check(runsEnemies(PeerPlace::Here, false, true, false), "guest first in the room keeps them");
    check(!runsEnemies(PeerPlace::Here, true, false, true), "host arriving second hands them over");
    for (const bool hostClaim : {true, false}) {
        for (const bool guestClaim : {true, false}) {
            const bool hostRuns = runsEnemies(PeerPlace::Here, true, hostClaim, guestClaim);
            const bool guestRuns = runsEnemies(PeerPlace::Here, false, guestClaim, hostClaim);
            check(hostRuns != guestRuns, "shared room: exactly one machine runs the enemies");
        }
    }
}

}  // namespace

int main() {
    testKeyNames();
    testParse();
    testRealConfigFormat();
    testOwnership();
    testPresence();
    testEnemyAuthority();
    if (g_failures == 0) std::printf("command_rules_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
