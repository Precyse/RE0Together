// The milestone 13 trace rules (no game): fade edges, text cleaning, slot lists and the per-name limit of the Lua error lines.
#include <cstdio>
#include <string>

#include "../src/script_trace_rules.h"

namespace {

using namespace script_trace_rules;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

FadeSnapshot fade(uint32_t state, float percent, int32_t timer, int32_t pauseCount = 0, uint8_t ignorePause = 0) {
    FadeSnapshot snapshot;
    snapshot.state = state;
    snapshot.percent = percent;
    snapshot.timer = timer;
    snapshot.pauseCount = pauseCount;
    snapshot.ignorePause = ignorePause;
    return snapshot;
}

bool quiet(const FadeEdges& edges) { return !edges.stateChanged && !edges.complete && !edges.held && !edges.released; }

}  // namespace

int main() {
    FadeWatch watch;
    check(quiet(watch.observe(fade(0, 0, -1))), "no fade: nothing to report");
    const FadeEdges started = watch.observe(fade(2, 1.0f, 0));
    check(started.stateChanged && started.stateBefore == 0 && !started.complete && !started.held, "a fade in starts: the state edge");
    check(quiet(watch.observe(fade(2, 0.6f, 300))), "a fade that runs is not reported every tick");
    const FadeEdges done = watch.observe(fade(0, 0, -1));
    check(done.stateChanged && done.stateBefore == 2 && done.complete, "the fade in clears the state and completes");
    check(quiet(watch.observe(fade(0, 0, -1))), "afterwards nothing is reported");

    watch.observe(fade(1, 0.5f, 400));
    const FadeEdges out = watch.observe(fade(1, 1.0f, -1));
    check(out.complete && !out.stateChanged, "a fade out completes and keeps its state (the screen stays black)");

    FadeWatch paused;
    paused.observe(fade(2, 1.0f, 0));
    const FadeEdges held = paused.observe(fade(2, 1.0f, 10, 2));
    check(held.held && !held.released, "a running fade with a pause count waits: reported once");
    check(quiet(paused.observe(fade(2, 1.0f, 10, 2))) && quiet(paused.observe(fade(2, 1.0f, 10, 3))), "it is not reported again while it waits");
    const FadeEdges released = paused.observe(fade(2, 0.9f, 20, 0));
    check(released.released && !released.held, "the pause count back at zero releases it");

    FadeWatch ignoring;
    ignoring.observe(fade(2, 1.0f, 0, 2, 1));
    check(!heldByPause(fade(2, 1.0f, 0, 2, 1)), "a fade that ignores the pause is never held");
    check(!heldByPause(fade(2, 1.0f, -1, 2, 0)) && !heldByPause(fade(0, 0, 0, 2, 0)), "no running fade, nothing to hold");

    FadeWatch endsWhileHeld;
    endsWhileHeld.observe(fade(2, 1.0f, 5, 1));
    const FadeEdges ended = endsWhileHeld.observe(fade(0, 0, -1, 1));
    check(ended.released && ended.complete, "a held fade that ends is released");

    check(clipped("abc\0def", 7, 64) == "abc", "text is cut at the NUL");
    check(clipped("abcdef", 6, 4) == "abcd", "text is cut at the limit");
    check(clipped("a\tb\x7F", 4, 64) == "a?b?", "control characters show as ?");
    check(isPrintableName("tss03_start", 12) && !isPrintableName("", 1) && !isPrintableName("a\x01", 3) && !isPrintableName("a\xE9", 3),
          "a thread name is logged only when it is printable ASCII");

    const uint8_t slots[] = {1, 12, 3};
    check(slotListText(slots, 3) == "{1,12,3}" && slotListText(slots, 0) == "{}" && slotListText(slots, 1) == "{1}", "slot lists");

    NamedRateLimit limit;
    uint32_t triggers = 0;
    check(limit.allow("tss03_start", 1000, triggers) && triggers == 1, "the first error of a thread is written");
    check(!limit.allow("tss03_start", 1500, triggers), "a repeat within a second is held back");
    check(limit.allow("tss04_start", 1500, triggers), "another thread has its own limit");
    check(limit.allow("tss03_start", 2100, triggers) && triggers == 2, "after a second the thread writes again and counts the repeat");
    NamedRateLimit crowded;
    for (int i = 0; i < 40; ++i) crowded.allow("thread" + std::to_string(i), 0, triggers);
    check(crowded.allow("thread0", 10, triggers), "when the names run out the oldest is forgotten");
    return g_failures == 0 ? 0 : 1;
}
