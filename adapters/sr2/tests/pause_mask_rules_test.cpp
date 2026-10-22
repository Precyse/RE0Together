// The pause flag rules of milestones 9 and 12 (no game): a bit per session slot, and the game's apply only on the edges.
#include <cstdio>

#include "../src/pause_mask_rules.h"
#include "check.h"

int main() {
    using namespace pause_mask_rules;

    check(bitOf(1) == 0x02 && bitOf(7) == 0x80 && bitOf(9) == 0x80, "a bit per slot, a slot above 7 shares the last bit");

    const Change first = afterMessage(0, 1, true, true);
    check(first.before == 0 && first.after == 0x02 && first.apply, "zero to set: apply");
    const Change repeat = afterMessage(0x02, 1, true, true);
    check(repeat.after == 0x02 && !repeat.apply, "the same player sends 1 twice: no second apply");
    const Change resume = afterMessage(0x02, 1, false, true);
    check(resume.after == 0 && resume.apply, "set to zero: apply");
    const Change second = afterMessage(0x02, 2, true, true);
    check(second.after == 0x06 && !second.apply, "a second pauser changes the byte, no apply");
    check(afterMessage(0x06, 1, false, true).after == 0x04 && !afterMessage(0x06, 1, false, true).apply,
          "one of two resumes: the other still pauses, no apply");
    check(afterMessage(0x04, 2, false, true).after == 0 && afterMessage(0x04, 2, false, true).apply, "the last pauser resumes: apply");
    check(!afterMessage(0, 1, false, true).apply, "a resume with nobody paused: no apply");

    const Change early = afterMessage(0, 1, true, false);
    check(early.after == 0x02 && !early.apply, "the local member is not in game: the byte changes, no apply");
    check(!afterMessage(0x02, 1, false, false).apply, "not in game: a resume does not apply either");

    const Change one = afterLeave(0x02, 1);
    check(one.before == 0x02 && one.after == 0 && one.apply, "the only pausing member leaves: apply");
    const Change two = afterLeave(0x06, 1);
    check(two.after == 0x04 && !two.apply, "one of two pausing members leaves: no apply");
    const Change other = afterLeave(0x04, 1);
    check(other.after == 0x04 && !other.apply, "a member that did not pause leaves: nothing changes");
    check(!afterLeave(0, 1).apply, "nobody paused: nothing to apply");

    // Counting the applies of a run: the game's count goes up on a set and down on a clear, so a run balances when the
    // applies alternate and end with the byte at zero.
    uint8_t mask = 0;
    int applied = 0;
    auto step = [&](const Change& change) {
        mask = change.after;
        if (change.apply) ++applied;
    };
    step(afterMessage(mask, 1, true, true));
    step(afterMessage(mask, 2, true, true));
    step(afterMessage(mask, 2, true, true));
    step(afterMessage(mask, 1, false, true));
    step(afterMessage(mask, 2, false, true));
    check(mask == 0 && applied == 2, "P2 and P3 pause together, P3 repeats, both resume: one apply at the start and one at the end");
    step(afterMessage(mask, 1, true, true));
    step(afterLeave(mask, 1));
    check(mask == 0 && applied == 4, "a pausing member leaves while paused: one apply to pause, one to resume");

    check(afterMessage(0, 1, true, true).apply && afterMessage(0x02, 1, false, true).apply &&
              !afterMessage(0x02, 1, true, true).apply,
          "with one remote the byte is zero or not as the shipped messages have it, except a repeated 1");
    return g_failures == 0 ? 0 : 1;
}
