// Checks the developer command parser (no game): which lines are commands. Exit 0 when every check passes.
#include <cstdio>
#include <string>

#include "../src/dev_command_rule.h"

namespace {

namespace rule = dev_command_rule;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testStall() {
    const rule::Parsed parsed = rule::parse("stall 25000\n");
    check(parsed.commands.size() == 1 && parsed.refused.empty(), "one stall command");
    check(parsed.commands[0].kind == rule::Kind::Stall && parsed.commands[0].stallMs == 25000, "stall keeps its milliseconds");
    check(rule::parse("stall 1").commands[0].stallMs == 1, "stall of 1 ms is the lowest");
    check(rule::parse("stall 60000").commands[0].stallMs == rule::kMaxStallMs, "stall of 60000 ms is the highest");
}

void testStallRefusals() {
    for (const char* line : {"stall", "stall 0", "stall 60001", "stall -5", "stall 12x", "stall 1 2", "stall 100000", "stall 0x10",
                             "stall 2.5"}) {
        const rule::Parsed parsed = rule::parse(line);
        check(parsed.commands.empty() && parsed.refused.size() == 1, line);
    }
}

void testLockAndRescue() {
    const rule::Parsed parsed = rule::parse("lock\nrescue\n");
    check(parsed.commands.size() == 2, "lock and rescue parse");
    check(parsed.commands[0].kind == rule::Kind::Lock && parsed.commands[1].kind == rule::Kind::Rescue, "in file order");
    check(rule::parse("lock now").commands.empty(), "lock takes no argument");
    check(rule::parse("rescue 1").commands.empty(), "rescue takes no argument");
}

void testAnythingElseIsRefused() {
    const rule::Parsed parsed = rule::parse("Stall 100\nreboot\nstalling 5\n");
    check(parsed.commands.empty(), "names are exact");
    check(parsed.refused.size() == 3 && parsed.refused[1] == "reboot", "each bad line is kept for the log");
}

void testLayout() {
    const rule::Parsed parsed = rule::parse("\r\n  stall   300 \r\n\r\n\tlock\t\nbad line\n");
    check(parsed.commands.size() == 2 && parsed.commands[0].stallMs == 300, "blanks, tabs and CR LF are ignored");
    check(parsed.refused.size() == 1 && parsed.refused[0] == "bad line", "blank lines are not refused");
    check(rule::parse("").commands.empty() && rule::parse("").refused.empty(), "an empty file is nothing");
    check(rule::parse("lock").commands.size() == 1, "the last line needs no newline");
}

void testNames() {
    check(std::string(rule::name(rule::Kind::Stall)) == "stall", "stall name");
    check(std::string(rule::name(rule::Kind::Lock)) == "lock", "lock name");
    check(std::string(rule::name(rule::Kind::Rescue)) == "rescue", "rescue name");
}

}  // namespace

int main() {
    testStall();
    testStallRefusals();
    testLockAndRescue();
    testAnythingElseIsRefused();
    testLayout();
    testNames();
    if (g_failures == 0) std::printf("dev_command_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
