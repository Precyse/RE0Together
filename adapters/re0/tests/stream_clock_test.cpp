// Checks the shared stream clock (no game): the smallest-transit offset, its relaxing, a restarted peer clock, the time
// to show, re-anchoring and the generation streams follow. Exit 0 when every check passes.
#include <cstdio>

#include "../src/stream_clock.h"

namespace {

namespace sc = stream_clock;

constexpr uint32_t kTransitMs = 40;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testTarget() {
    sc::StreamClock clock;
    check(!clock.ready(), "a new clock has no offset");
    clock.observe(1000, 1000 + kTransitMs);
    check(clock.ready(), "the first sample sets the offset");
    check(clock.target(1000 + kTransitMs + sc::kRenderDelayMs) == 1000, "the shown time is the render delay behind the mapped time");
    check(clock.target(5000) == 5000 - kTransitMs - sc::kRenderDelayMs, "the shown time follows the local clock");
}

void testSmallestTransit() {
    sc::StreamClock clock;
    clock.observe(1000, 1060);
    clock.observe(1033, 1073);  // transit 40: the faster path wins
    clock.observe(1066, 1156);  // transit 90: a slow packet does not move it
    check(clock.target(1033 + 40 + sc::kRenderDelayMs) == 1033, "the smallest transit time maps the clocks");
}

void testRelax() {
    sc::StreamClock clock;
    clock.observe(1000, 1040);
    for (uint32_t i = 0; i < sc::kOffsetRelaxSamples - 1; ++i) clock.observe(2000 + i, 2000 + i + 60);
    check(clock.target(5000) == 5000 - 40 - sc::kRenderDelayMs, "later samples have not moved it yet");
    clock.observe(3000, 3060);
    check(clock.target(5000) == 5000 - 41 - sc::kRenderDelayMs, "enough later samples move it 1 ms");
}

void testRestart() {
    sc::StreamClock clock;
    clock.observe(500000, 500040);
    const uint32_t before = clock.generation();
    clock.observe(100, 800000 + 140);  // the peer's game restarted: its clock is far off
    check(clock.generation() != before, "a restarted peer clock changes the generation");
    check(clock.target(800000 + 140 + sc::kRenderDelayMs) == 100, "and the clock maps the new one");
}

void testGenerationOnFirstSample() {
    sc::StreamClock clock;
    const uint32_t before = clock.generation();
    clock.observe(1000, 1040);
    check(clock.generation() == before, "the first sample is not a change");
    clock.observe(1010, 1040);  // transit 30: a smaller one is not a change either
    check(clock.generation() == before, "a smaller transit time is not a change");
}

void testReanchorAndClear() {
    sc::StreamClock clock;
    clock.observe(1000, 1040);
    const uint32_t before = clock.generation();
    clock.reanchor(90);
    check(clock.generation() != before, "re-anchoring changes the generation");
    check(clock.target(2000) == 2000 - 90 - sc::kRenderDelayMs, "the given transit time is the offset whatever was tracked");
    const uint32_t anchored = clock.generation();
    clock.clear();
    check(!clock.ready() && clock.generation() != anchored, "clearing forgets the offset and changes the generation");
    clock.observe(10, 60);
    check(clock.ready() && clock.target(60 + sc::kRenderDelayMs) == 10, "it tracks again after a clear");
}

void testWrap() {
    sc::StreamClock clock;
    constexpr uint32_t kBeforeWrap = 0xffffffffu - 20;
    clock.observe(kBeforeWrap, kBeforeWrap + kTransitMs);
    check(clock.target(kBeforeWrap + kTransitMs + sc::kRenderDelayMs + 50) == kBeforeWrap + 50, "the mapping works across the 32-bit wrap");
    check(sc::after(5, 0xfffffff0u) && !sc::after(0xfffffff0u, 5) && !sc::after(7, 7), "after is wrap safe");
}

}  // namespace

int main() {
    testTarget();
    testSmallestTransit();
    testRelax();
    testRestart();
    testGenerationOnFirstSample();
    testReanchorAndClear();
    testWrap();
    if (g_failures == 0) std::printf("stream_clock_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
