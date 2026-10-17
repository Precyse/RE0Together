// The guest net clock rules of milestone 11 B1 (no game): forward steps, backward steps as debt, the catch-up, the host as
// the only source, the cap, and the 2-player case.
#include <cstdio>

#include "../src/net_clock_rules.h"

namespace {

using namespace net_clock_rules;

constexpr uint32_t kNow = 100000;
constexpr uint32_t kFrameMs = 33;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    const Clock forward = onClock(kNow, kNow + 40, true, 0);
    check(forward.outcome == Outcome::Forward && forward.time == kNow + 40 && forward.debt == 0,
          "a clock ahead of T is taken, as the game does");
    const Clock same = onClock(kNow, kNow, true, 70);
    check(same.outcome == Outcome::Forward && same.time == kNow && same.debt == 0, "a clock equal to T is a forward step and clears the debt");

    const Clock behind = onClock(kNow, kNow - 120, true, 0);
    check(behind.outcome == Outcome::Absorbed && behind.time == kNow && behind.debt == 120, "a clock behind T leaves T and becomes debt");
    const Clock again = onClock(kNow + 500, kNow + 450, true, 120);
    check(again.debt == 50, "the debt is measured again against the current T, not added up");

    const Clock stranger = onClock(kNow, kNow + 500, false, 30);
    check(stranger.outcome == Outcome::Ignored && stranger.time == kNow && stranger.debt == 30 && needsLog(stranger.outcome),
          "a clock from a member that is not the host changes nothing and is logged");
    const Clock strangerBehind = onClock(kNow, kNow - 500, false, 0);
    check(strangerBehind.time == kNow && strangerBehind.debt == 0, "a non-host clock behind T makes no debt either");

    check(onClock(kNow, kNow - kMaxClockDebtMs, true, 0).outcome == Outcome::Absorbed, "a debt of exactly the cap is still absorbed");
    const Clock resync = onClock(kNow, kNow - kMaxClockDebtMs - 1, true, 400);
    check(resync.outcome == Outcome::Resynced && resync.time == kNow - kMaxClockDebtMs - 1 && resync.debt == 0 && needsLog(resync.outcome),
          "a debt above the cap is a real resync: T is set as the game would, and it is logged");
    check(!needsLog(Outcome::Forward) && !needsLog(Outcome::Absorbed), "forward and absorbed clocks are not logged");

    const Tick partial = onTick(kNow + kFrameMs, 100, kFrameMs);
    check(partial.time == kNow + kFrameMs - kFrameMs / kCatchUpDivisor && partial.debt == 100 - kFrameMs / kCatchUpDivisor,
          "a tick pays at most half of the time it added");
    const Tick last = onTick(kNow + kFrameMs, 5, kFrameMs);
    check(last.debt == 0 && last.time == kNow + kFrameMs - 5, "the last of a debt takes only what is owed");
    const Tick none = onTick(kNow + kFrameMs, 0, kFrameMs);
    check(none.time == kNow + kFrameMs && none.debt == 0, "no debt: the tick is the game's");
    check(onTick(kNow, 90, 1).time == kNow && onTick(kNow, 90, 0).time == kNow, "a tick that added under two milliseconds pays nothing");

    uint32_t time = kNow;
    uint32_t debt = 300;
    bool neverBack = true;
    int ticks = 0;
    while (debt > 0 && ticks < 100) {
        const uint32_t before = time;
        time += kFrameMs;
        const Tick paid = onTick(time, debt, tickAdvance(before, time));
        time = paid.time;
        debt = paid.debt;
        neverBack = neverBack && time > before;
        ++ticks;
    }
    check(debt == 0 && neverBack, "T moves forward on every tick while the debt is paid");
    check(ticks > 300 / kFrameMs && ticks <= 2 * 300 / kFrameMs + 1, "a delay costs about twice its length in ticks at half speed");

    check(tickAdvance(kNow, kNow + kFrameMs) == kFrameMs && tickAdvance(kNow, kNow + kMaxTickAdvanceMs) == kMaxTickAdvanceMs,
          "a tick's advance is the frame time up to the game's cap");
    check(tickAdvance(kNow, kNow) == 0 && tickAdvance(kNow, kNow + kMaxTickAdvanceMs + 1) == 0 && tickAdvance(kNow, kNow - 5) == 0,
          "no advance, an advance above the cap and a step back are not a tick");

    check(!governs(true, 3) && !governs(true, 0), "the host is never governed");
    check(!governs(false, 1) && !governs(false, 0), "a guest with one remote (2-player play) keeps the game's assignment");
    check(governs(false, 2) && governs(false, 3), "a guest with two or more remotes is governed");
    return g_failures == 0 ? 0 : 1;
}
