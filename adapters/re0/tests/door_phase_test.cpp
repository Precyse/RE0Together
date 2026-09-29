// Checks the door phase predicate (no game). Exit 0 when every check passes.
#include <cstdio>
#include <initializer_list>

#include "../src/door_phase.h"

int main() {
    int failures = 0;
    const auto expect = [&failures](int32_t phase, bool running) {
        if (door_phase::running(phase) == running) return;
        std::printf("FAIL: phase %d running=%d\n", phase, !running);
        ++failures;
    };
    for (const int32_t phase : {0, 1, 2, 3, 4}) expect(phase, true);
    expect(5, false);
    expect(-1, false);
    expect(static_cast<int32_t>(0xFFFFFFFFu), false);
    expect(6, false);
    if (failures == 0) std::printf("door_phase_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
