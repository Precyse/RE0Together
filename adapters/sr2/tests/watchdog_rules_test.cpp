// The game thread watchdog's rules (no game): one report per stall and one when it ends, the dumps kept, the dump names.
#include <string>
#include <vector>

#include "../src/watchdog_rules.h"
#include "check.h"

using watchdog_rules::Event;
using watchdog_rules::kStallMs;
using watchdog_rules::Monitor;

int main() {
    Monitor monitor;
    check(monitor.step(50000, 0) == Event::None, "nothing before the first frame");
    check(monitor.step(10000 + kStallMs - 1, 10000) == Event::None, "a frame gap just below 10 s is no stall");
    check(monitor.step(10000 + kStallMs, 10000) == Event::Stalled, "10 s without a frame is a stall");
    check(monitor.step(10000 + kStallMs + 5000, 10000) == Event::None, "a stall is reported once");
    check(monitor.step(43000, 43000) == Event::Resumed && monitor.stalledFor() == 33000,
          "the next frame ends it, with the gap between the two frames (33 s, P3 at 00:42:30)");
    check(monitor.step(44000, 43000) == Event::None && monitor.step(43000 + kStallMs, 43000) == Event::Stalled,
          "a later stall is reported again");

    const std::vector<std::string> three = {"20261022-004925", "20261022-003208", "20261022-004230"};
    const std::vector<std::string> deleted = watchdog_rules::stemsToDelete(three, watchdog_rules::kKeptDumps);
    check(deleted == std::vector<std::string>{"20261022-003208"}, "with three kept, the oldest goes before a fourth is written");
    check(watchdog_rules::stemsToDelete({"20261022-003208", "20261022-004230"}, watchdog_rules::kKeptDumps).empty(),
          "two dumps leave room for a third");
    check(watchdog_rules::stemsToDelete({"b", "a", "d", "c", "e"}, 3) == std::vector<std::string>{"a", "b", "c"},
          "any number above it is cut to two before the new one");
    check(watchdog_rules::stem(2026, 10, 22, 0, 49, 25) == "20261022-004925", "a dump is named by its local time, which sorts");
    return g_failures == 0 ? 0 : 1;
}
