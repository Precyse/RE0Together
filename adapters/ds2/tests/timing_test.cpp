// Timing: the clock offset estimate from ping/pong samples, the adaptive render delay, and the delay line's
// interpolation (no game, no network).
#include <cmath>
#include <cstdlib>
#include <cstdio>

#include "../src/adaptive_delay.h"
#include "../src/clock_offset.h"
#include "../src/delay_line.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

constexpr TimeUs kMs = 1000;

bool near(TimeUs a, TimeUs b) { return std::llabs(a - b) <= kMs; }  // the baseline creeps up a few microseconds

// A peer whose clock reads `offset` ahead of ours, `rtt` away; the peer answers in the middle of the round trip.
void exchange(ClockOffset& clock, TimeUs sentAt, TimeUs rtt, TimeUs offset) {
    clock.addSample(sentAt, sentAt + rtt, sentAt + rtt / 2 + offset);
}

void clockOffset() {
    ClockOffset clock;
    check(!clock.valid(), "no estimate before any sample");
    exchange(clock, 1'000'000, 40 * kMs, 5'000'000);
    exchange(clock, 2'000'000, 40 * kMs, 5'000'000);
    check(!clock.valid(), "two samples are not enough");
    exchange(clock, 3'000'000, 40 * kMs, 5'000'000);
    check(clock.valid() && clock.offsetUs() == 5'000'000, "a symmetric link gives the exact offset");
    check(clock.rttUs() == 40 * kMs, "and the round trip");
    check(clock.toLocal(8'000'000) == 3'000'000 && clock.toPeer(3'000'000) == 8'000'000, "conversion goes both ways");

    exchange(clock, 4'000'000, 900 * kMs, 5'000'000 + 400 * kMs);  // one badly delayed, lopsided round trip
    check(clock.offsetUs() == 5'000'000, "one outlier does not move the median");
    check(!clock.addSample(5'000'000, 4'000'000, 0), "a pong that arrives before its ping left is refused");
    check(!clock.addSample(1'000'000, 1'000'000 + ClockOffset::kMaxRttUs + 1, 0), "a huge round trip is refused");

    ClockOffset moving;
    for (int i = 0; i < 40; ++i) exchange(moving, i * 1'000'000, 30 * kMs, 1'000'000 + i * 100);
    check(std::abs(moving.offsetUs() - (1'000'000 + 33 * 100)) < 600, "the median follows a slow drift over the newest samples");
    check(moving.samples() == ClockOffset::kSamples, "only the newest samples are kept");
}

// Feeds a steady stream: one report every `period`, each arriving `transit` after it was sent, plus `extra(i)`.
template <class Extra>
void stream(AdaptiveDelay& delay, int reports, TimeUs period, TimeUs transit, TimeUs start, Extra extra) {
    for (int i = 0; i < reports; ++i) {
        const TimeUs sent = start + i * period;
        delay.onArrival(sent, sent + transit + extra(i));
    }
}

void adaptiveDelay() {
    AdaptiveDelay delay;
    check(delay.delayUs() == AdaptiveDelay::kMinDelayUs, "starts at the minimum");
    stream(delay, 300, 33 * kMs, 40 * kMs, 0, [](int) { return TimeUs{0}; });
    check(delay.delayUs() == AdaptiveDelay::kMinDelayUs, "a steady link stays at the minimum, whatever the transit");

    const TimeUs spikeAt = 300 * 33 * kMs;
    delay.onArrival(spikeAt, spikeAt + 40 * kMs + 60 * kMs);
    check(near(delay.delayUs(), AdaptiveDelay::kMinDelayUs + 2 * 60 * kMs), "a 60 ms late report grows the delay at once");
    delay.onArrival(spikeAt + 33 * kMs, spikeAt + 33 * kMs + 40 * kMs + 500 * kMs);
    check(delay.delayUs() == AdaptiveDelay::kMaxDelayUs, "the delay is capped");

    AdaptiveDelay shrinking = delay;
    const TimeUs calmFrom = spikeAt + 100 * kMs;
    stream(shrinking, 200, 33 * kMs, 40 * kMs, calmFrom, [](int) { return TimeUs{0}; });
    check(shrinking.delayUs() < AdaptiveDelay::kMaxDelayUs && shrinking.delayUs() > AdaptiveDelay::kMinDelayUs,
          "after calm it shrinks, but slowly (one step per evaluation)");
    stream(shrinking, 3000, 33 * kMs, 40 * kMs, calmFrom + 200 * 33 * kMs, [](int) { return TimeUs{0}; });
    check(shrinking.delayUs() == AdaptiveDelay::kMinDelayUs, "and gets back to the minimum after a long calm");

    AdaptiveDelay jittery;
    stream(jittery, 600, 33 * kMs, 40 * kMs, 0, [](int i) { return TimeUs{i % 10 == 0 ? 30 * kMs : 0}; });
    check(near(jittery.delayUs(), AdaptiveDelay::kMinDelayUs + 2 * 30 * kMs), "a link with regular 30 ms spikes settles at minimum + 2 x 30 ms");
}

void delayLine() {
    DelayLine<float> line;
    check(!line.at(1'000'000), "an empty line has nothing to show");
    const TimeUs delay = line.delayUs();
    for (int i = 0; i < 10; ++i) {
        const TimeUs sent = 1'000'000 + i * 100 * kMs;  // 10 Hz reports of the value 0, 10, 20 ...
        line.push(sent, sent + 20 * kMs, static_cast<float>(i * 10));
    }
    const TimeUs now = 1'000'000 + 500 * kMs + delay;  // render time is the report 5, 500 ms in
    auto bracket = line.at(now);
    check(bracket && *bracket->from == 50.0f && bracket->alpha == 0.0f, "rendering exactly on a report shows it");
    bracket = line.at(now + 50 * kMs);
    check(bracket && *bracket->from == 50.0f && *bracket->to == 60.0f && std::fabs(bracket->alpha - 0.5f) < 1e-4f,
          "between two reports it is the pair and the fraction");
    bracket = line.at(1'000'000);
    check(bracket && *bracket->from == 0.0f && !bracket->starved, "before the oldest report the oldest is held");
    bracket = line.at(1'000'000 + 900 * kMs + delay + 100 * kMs);
    check(bracket && *bracket->from == 90.0f && bracket->starved, "past the newest report it is held and marked starved");

    DelayLine<float> reordered;
    reordered.push(2'000'000, 2'030'000, 2.0f);
    reordered.push(1'000'000, 2'040'000, 1.0f);
    reordered.push(3'000'000, 3'030'000, 3.0f);
    reordered.push(2'000'000, 3'040'000, 2.5f);
    const TimeUs render = 1'500'000 + reordered.delayUs();
    bracket = reordered.at(render);
    check(bracket && *bracket->from == 1.0f && *bracket->to == 2.5f, "late reports are put in order and a duplicate replaces");

    DelayLine<int> longRun;
    for (int i = 0; i < 1000; ++i) longRun.push(i * 16 * kMs, i * 16 * kMs + 20 * kMs, i);
    const auto newest = longRun.at(999 * 16 * kMs + longRun.delayUs());
    check(newest && *newest->from == 999, "a long stream keeps the newest reports");
    longRun.clear();
    check(longRun.empty() && longRun.delayUs() == AdaptiveDelay::kMinDelayUs, "clear forgets the reports and the delay");
}

}  // namespace

int main() {
    clockOffset();
    adaptiveDelay();
    delayLine();
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
