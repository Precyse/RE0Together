// The pure rules of the HUD blocks of milestone 16 wave 2 (no game): the turns of a block over the remotes, the remote that holds the
// back prompt and the trace text.
#include <cstdio>
#include <string>

#include "../src/hud_rules.h"

namespace {

using hud_rules::RemoteTurns;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

constexpr size_t kOneRemote = 1;
constexpr size_t kTwoRemotes = 2;
constexpr size_t kFourRemotes = 4;
constexpr uint8_t kHeld = 0x22;
constexpr uint8_t kIdle = 0x01;

// How many turns a pass over `count` remotes has, counting the first that `begin` opens.
size_t turnsOf(RemoteTurns& turns, size_t count) {
    if (!turns.begin(count)) return 0;
    size_t total = 1;
    while (turns.next()) ++total;
    return total;
}

void testTurns() {
    RemoteTurns turns;
    check(!turns.begin(0) && !turns.active(), "no remote: no turn, idle");
    check(turnsOf(turns, kOneRemote) == 1 && !turns.active(), "1 remote: one turn, idle after it");
    check(turnsOf(turns, kFourRemotes) == 4, "4 remotes: four turns");
    check(!turns.next() && !turns.active(), "next while idle: no turn");

    turns.begin(kTwoRemotes);
    check(turns.active() && turns.current() == 0, "begin opens turn 0");
    check(turns.next() && turns.current() == 1, "next opens turn 1");
    check(!turns.next() && !turns.active(), "next after the last turn ends the pass");

    turns.begin(kFourRemotes);
    turns.next();
    turns.next();
    turns.begin(kTwoRemotes);
    check(turns.active() && turns.current() == 0, "begin while active restarts at turn 0");
    check(turns.next() && !turns.next(), "the restarted pass has the new count");
    check(turnsOf(turns, kTwoRemotes) == 2, "begin again after the end runs a full pass");
}

void testFirstWithBehaviour() {
    const uint8_t none[] = {kIdle, kIdle, kIdle};
    const uint8_t first[] = {kHeld, kIdle, kHeld};
    const uint8_t last[] = {kIdle, kIdle, kHeld};
    check(hud_rules::firstWithBehaviour(none, 3, kHeld) == 3, "none holds the behaviour: count");
    check(hud_rules::firstWithBehaviour(first, 3, kHeld) == 0, "the first of two holders wins");
    check(hud_rules::firstWithBehaviour(last, 3, kHeld) == 2, "the last remote can hold it");
    check(hud_rules::firstWithBehaviour(last, 0, kHeld) == 0, "no remote: count 0");
}

void testSameHumans() {
    const uintptr_t a[] = {0x1A2B0000, 0x1A2C0000};
    const uintptr_t swapped[] = {0x1A2C0000, 0x1A2B0000};
    const uintptr_t shorter[] = {0x1A2B0000};
    check(hud_rules::sameHumans(a, 2, a, 2), "the same list is the same");
    check(!hud_rules::sameHumans(a, 2, swapped, 2), "order matters");
    check(!hud_rules::sameHumans(a, 2, shorter, 1), "a different count differs");
    check(hud_rules::sameHumans(a, 0, shorter, 0), "two empty lists are the same");
}

void testHumansLine() {
    const hud_rules::HumanText withFollowers[] = {{0x1A2B0000, 1, 3}, {0x1A2C0000, 2, 0}};
    check(hud_rules::humansLine(withFollowers, 2) == "0x1A2B0000 (slot 1, 3 followers), 0x1A2C0000 (slot 2, 0 followers)",
          "the minimap line names slot and followers");
    const hud_rules::HumanText plain[] = {{0x1A2B0000, 1, hud_rules::kNoFollowerCount}, {0x1A2C0000, 2, hud_rules::kNoFollowerCount}};
    check(hud_rules::humansLine(plain, 2) == "0x1A2B0000 (slot 1), 0x1A2C0000 (slot 2)", "the pause map line has no follower count");
    check(hud_rules::humansLine(plain, 0).empty(), "no human: an empty line");
}

}  // namespace

int main() {
    testTurns();
    testFirstWithBehaviour();
    testSameHumans();
    testHumansLine();
    return g_failures == 0 ? 0 : 1;
}
