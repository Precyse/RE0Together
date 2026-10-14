// The pause flag rules of milestone 9 (no game): a bit per session slot.
#include <cstdio>

#include "../src/pause_mask_rules.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    using namespace pause_mask_rules;

    check(afterMessage(0, 1, true) == 0x02 && afterMessage(0x02, 2, true) == 0x06, "each pausing member sets its own bit");
    check(afterMessage(0x06, 1, false) == 0x04 && afterMessage(0x04, 2, false) == 0, "resuming clears only the sender's bit");
    check(afterMessage(0x02, 1, true) == 0x02, "pausing twice changes nothing");
    check(afterMessage(0, 9, true) == 0x80 && afterMessage(0x80, 9, false) == 0, "a slot above 7 shares the last bit");

    const Left one = afterLeave(0x02, 1);
    check(one.mask == 0 && one.resumes, "the only pausing member leaves: the pause ends");
    const Left two = afterLeave(0x06, 1);
    check(two.mask == 0x04 && !two.resumes, "one of two pausing members leaves: the other still pauses");
    const Left other = afterLeave(0x04, 1);
    check(other.mask == 0x04 && !other.resumes, "a member that did not pause leaves: nothing changes");
    const Left none = afterLeave(0, 1);
    check(none.mask == 0 && !none.resumes, "nobody paused: nothing to resume");
    check(afterMessage(0, 1, true) != 0 && afterLeave(afterMessage(0, 1, true), 1).mask == 0,
          "with one remote the byte is zero or not, as the readers expect (the 2-player case)");
    return g_failures == 0 ? 0 : 1;
}
