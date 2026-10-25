#pragma once
#include <algorithm>
#include <cstdint>

// One peer's clock as this machine sees it (pure, unit tested): maps the peer's millisecond clock to the local one by
// the smallest transit time seen and gives the time to show, an adaptive render delay behind. Every stream from that
// peer (the puppet's poses and motion events, its weapon effects, enemy streams, box streams, incoming hits) reads the
// same instance, so everything this machine shows of the peer is the same instant.
//
// The delay is as small as the link allows (the idea of DS2's adaptive_delay.h, in this clock's milliseconds): each
// sample's lateness is its transit time above the fastest transit tracked, the worst lateness of a window sets the wanted
// delay (minimum + margin x lateness, clamped), and the shown delay slews towards it: up at most half a millisecond per
// local millisecond (the shown time slows, never steps back), down one millisecond per kShrinkEveryMs (a calm link
// earns a smaller delay slowly). It was a fixed 100 ms until 2026-10-25.
namespace stream_clock {

constexpr uint32_t kMinDelayMs = 50;         // two samples at the 40 Hz enemy and pose streams
constexpr uint32_t kMaxDelayMs = 250;
constexpr uint32_t kJitterMargin = 2;        // wanted delay = minimum + margin x the worst lateness of the window
constexpr uint32_t kWindowMs = 3000;         // how long the worst lateness is remembered before it may drop
constexpr uint32_t kGrowPerTwoMs = 1;        // the delay grows by at most 1 ms per 2 ms of local time
constexpr uint32_t kShrinkEveryMs = 100;     // and shrinks by 1 ms per this many ms of local time
constexpr int32_t kClockRestartMs = 5000;     // a transit time this far off the tracked one: the peer's clock restarted
constexpr uint32_t kOffsetRelaxSamples = 32;  // a later-than-tracked sample moves the tracked transit time 1 ms per this many

// Wrap-safe "a is later than b" for the peers' millisecond clocks and sequence numbers.
inline bool after(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) > 0; }

// The delay a window whose worst lateness was `latenessMs` asks for.
constexpr uint32_t wantedDelay(uint32_t latenessMs) {
    return std::clamp(kMinDelayMs + kJitterMargin * latenessMs, kMinDelayMs, kMaxDelayMs);
}

class StreamClock {
public:
    // Forgets the offset and the delay history. Streams that read the clock see the generation change and start over.
    void clear() {
        hasOffset_ = false;
        laterSamples_ = 0;
        resetDelay();
        ++generation_;
    }

    // Tracks the smallest transit time (arrival minus the peer's clock) and the delay. A transit time far off the tracked
    // one means the peer's clock restarted: the clock starts over and the generation changes.
    void observe(uint32_t senderMs, uint32_t arrivalMs) {
        const int32_t transit = static_cast<int32_t>(arrivalMs - senderMs);
        if (!hasOffset_) {
            setOffset(transit);
            startDelay(arrivalMs);
            return;
        }
        const int32_t gap = transit - offset_;
        if (gap > kClockRestartMs || gap < -kClockRestartMs) {
            reanchor(transit);
            startDelay(arrivalMs);
            return;
        }
        if (gap < 0) {
            offset_ = transit;
        } else if (gap > 0 && ++laterSamples_ >= kOffsetRelaxSamples) {
            ++offset_;
            laterSamples_ = 0;
        }
        trackDelay(gap > 0 ? static_cast<uint32_t>(gap) : 0, arrivalMs);
    }

    // Takes `transit` as the offset whatever was tracked (the newest sample is the truth); the generation changes.
    void reanchor(int32_t transit) {
        setOffset(transit);
        ++generation_;
    }

    bool ready() const { return hasOffset_; }

    // The render delay now.
    uint32_t delayMs() const { return delay_; }

    // The peer's time to show at local time `nowMs`.
    uint32_t target(uint32_t nowMs) const { return nowMs - static_cast<uint32_t>(offset_) - delay_; }

    // Changes whenever the clock is cleared or re-anchored: a stream that last saw another one starts over.
    uint32_t generation() const { return generation_; }

private:
    void setOffset(int32_t transit) {
        offset_ = transit;
        hasOffset_ = true;
        laterSamples_ = 0;
    }

    void resetDelay() {
        delay_ = kMinDelayMs;
        windowPeak_ = 0;
        delayStarted_ = false;
    }

    void startDelay(uint32_t nowMs) {
        if (delayStarted_) return;
        delayStarted_ = true;
        windowStart_ = lastSlewMs_ = nowMs;
    }

    void trackDelay(uint32_t latenessMs, uint32_t nowMs) {
        startDelay(nowMs);
        windowPeak_ = std::max(windowPeak_, latenessMs);
        if (nowMs - windowStart_ >= kWindowMs) {
            wanted_ = wantedDelay(windowPeak_);
            windowStart_ = nowMs;
            windowPeak_ = latenessMs;
        }
        wanted_ = std::max(wanted_, wantedDelay(windowPeak_));
        slew(nowMs);
    }

    // Moves the shown delay towards the wanted one at the allowed rates.
    void slew(uint32_t nowMs) {
        if (!after(nowMs, lastSlewMs_)) return;  // a sample handed in out of local order moves nothing
        const uint32_t elapsed = nowMs - lastSlewMs_;
        if (delay_ < wanted_) {
            const uint32_t step = elapsed * kGrowPerTwoMs / 2;
            if (step == 0) return;
            delay_ = std::min(wanted_, delay_ + step);
        } else if (delay_ > wanted_) {
            const uint32_t step = elapsed / kShrinkEveryMs;
            if (step == 0) return;
            delay_ = std::max(wanted_, delay_ - std::min(step, delay_ - wanted_));
        }
        lastSlewMs_ = nowMs;
    }

    bool hasOffset_ = false;
    int32_t offset_ = 0;
    uint32_t laterSamples_ = 0;
    uint32_t generation_ = 0;
    uint32_t delay_ = kMinDelayMs;   // the delay shown
    uint32_t wanted_ = kMinDelayMs;  // what the recent lateness asks for
    uint32_t windowPeak_ = 0;        // the worst lateness of the current window
    uint32_t windowStart_ = 0;
    uint32_t lastSlewMs_ = 0;
    bool delayStarted_ = false;
};

}  // namespace stream_clock
