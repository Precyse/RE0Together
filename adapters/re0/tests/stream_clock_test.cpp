// Checks the shared stream clock (no game): the smallest-transit offset, its relaxing, a restarted peer clock, the time
// to show, re-anchoring and the generation streams follow. Exit 0 when every check passes.
#include <algorithm>
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
    check(clock.target(1000 + kTransitMs + sc::kMinDelayMs) == 1000, "the shown time is the render delay behind the mapped time");
    check(clock.target(5000) == 5000 - kTransitMs - sc::kMinDelayMs, "the shown time follows the local clock");
}

void testSmallestTransit() {
    sc::StreamClock clock;
    clock.observe(1000, 1060);
    clock.observe(1033, 1073);  // transit 40: the faster path wins
    clock.observe(1066, 1156);  // transit 90: a slow packet does not move it
    check(clock.target(1033 + 40 + clock.delayMs()) == 1033, "the smallest transit time maps the clocks");
}

void testRelax() {
    sc::StreamClock clock;
    clock.observe(1000, 1040);
    for (uint32_t i = 0; i < sc::kOffsetRelaxSamples - 1; ++i) clock.observe(2000 + i, 2000 + i + 60);
    check(clock.target(5000) == 5000 - 40 - clock.delayMs(), "later samples have not moved it yet");
    clock.observe(3000, 3060);
    check(clock.target(5000) == 5000 - 41 - clock.delayMs(), "enough later samples move it 1 ms");
}

void testRestart() {
    sc::StreamClock clock;
    clock.observe(500000, 500040);
    const uint32_t before = clock.generation();
    clock.observe(100, 800000 + 140);  // the peer's game restarted: its clock is far off
    check(clock.generation() != before, "a restarted peer clock changes the generation");
    check(clock.target(800000 + 140 + sc::kMinDelayMs) == 100, "and the clock maps the new one");
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
    check(clock.target(2000) == 2000 - 90 - sc::kMinDelayMs, "the given transit time is the offset whatever was tracked");
    const uint32_t anchored = clock.generation();
    clock.clear();
    check(!clock.ready() && clock.generation() != anchored, "clearing forgets the offset and changes the generation");
    clock.observe(10, 60);
    check(clock.ready() && clock.target(60 + sc::kMinDelayMs) == 10, "it tracks again after a clear");
}

void testWrap() {
    sc::StreamClock clock;
    constexpr uint32_t kBeforeWrap = 0xffffffffu - 20;
    clock.observe(kBeforeWrap, kBeforeWrap + kTransitMs);
    check(clock.target(kBeforeWrap + kTransitMs + sc::kMinDelayMs + 50) == kBeforeWrap + 50, "the mapping works across the 32-bit wrap");
    check(sc::after(5, 0xfffffff0u) && !sc::after(0xfffffff0u, 5) && !sc::after(7, 7), "after is wrap safe");
}

// The adaptive delay: the minimum on a calm link, it grows with lateness at a bounded rate (the shown time never steps
// back), holds through a window, and shrinks slowly once the link is calm again.
void testAdaptiveDelay() {
    sc::StreamClock clock;
    constexpr uint32_t kStepMs = 25;
    uint32_t sender = 1000;
    for (int i = 0; i < 40; ++i, sender += kStepMs) clock.observe(sender, sender + 20);
    check(clock.delayMs() == sc::kMinDelayMs, "a calm link keeps the minimum delay");

    uint32_t now = sender - kStepMs + 20;  // the last arrival of the calm stretch
    uint32_t lastTarget = clock.target(now);
    bool monotonic = true;
    for (int i = 0; i < 40; ++i, sender += kStepMs) {
        const uint32_t late = (i % 4 == 0) ? 20 : 0;  // every fourth sample 20 ms late (arrivals stay in order)
        now = std::max(now, sender + 20 + late);       // the local clock never runs back
        clock.observe(sender, sender + 20 + late);
        const uint32_t target = clock.target(now);
        monotonic = monotonic && !sc::after(lastTarget, target);
        lastTarget = target;
    }
    check(clock.delayMs() > sc::kMinDelayMs && clock.delayMs() <= sc::wantedDelay(20), "lateness grows the delay up to what it asks for");
    check(monotonic, "a growing delay never moves the shown time back");

    const uint32_t grown = clock.delayMs();
    for (int i = 0; i < 40; ++i, sender += kStepMs) clock.observe(sender, sender + 20);
    check(clock.delayMs() >= grown - 1, "the delay holds for the rest of the window");
    for (int i = 0; i < 400; ++i, sender += kStepMs) clock.observe(sender, sender + 20);
    check(clock.delayMs() < grown, "a calm stretch shrinks it");
    check(clock.delayMs() >= sc::kMinDelayMs, "never below the minimum");
    check(sc::wantedDelay(1000) == sc::kMaxDelayMs, "a very late link is capped at the maximum");
}

}  // namespace

int main() {
    testAdaptiveDelay();
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
