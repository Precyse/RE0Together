#pragma once
#include <cstdint>

// One peer's clock as this machine sees it (pure, unit tested): maps the peer's millisecond clock to the local one by
// the smallest transit time seen and gives the time to show, `kRenderDelayMs` behind. Every stream from that peer (the
// puppet's poses and motion events, later its weapon effects and enemy streams) reads the same instance, so everything
// this machine shows of the peer is the same instant. DS2's delay_line.h / adaptive_delay.h / clock_offset.h are a
// separate policy and stay in DS2.
namespace stream_clock {

constexpr uint32_t kRenderDelayMs = 100;
constexpr int32_t kClockRestartMs = 5000;     // a transit time this far off the tracked one: the peer's clock restarted
constexpr uint32_t kOffsetRelaxSamples = 32;  // a later-than-tracked sample moves the tracked transit time 1 ms per this many

// Wrap-safe "a is later than b" for the peers' millisecond clocks and sequence numbers.
inline bool after(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) > 0; }

class StreamClock {
public:
    // Forgets the offset. Streams that read the clock see the generation change and start over.
    void clear() {
        hasOffset_ = false;
        laterSamples_ = 0;
        ++generation_;
    }

    // Tracks the smallest transit time (arrival minus the peer's clock). A transit time far off the tracked one means
    // the peer's clock restarted: the clock starts over and the generation changes.
    void observe(uint32_t senderMs, uint32_t arrivalMs) {
        const int32_t transit = static_cast<int32_t>(arrivalMs - senderMs);
        if (!hasOffset_) {
            setOffset(transit);
            return;
        }
        const int32_t gap = transit - offset_;
        if (gap > kClockRestartMs || gap < -kClockRestartMs) {
            reanchor(transit);
        } else if (gap < 0) {
            offset_ = transit;
        } else if (gap > 0 && ++laterSamples_ >= kOffsetRelaxSamples) {
            ++offset_;
            laterSamples_ = 0;
        }
    }

    // Takes `transit` as the offset whatever was tracked (the newest sample is the truth); the generation changes.
    void reanchor(int32_t transit) {
        setOffset(transit);
        ++generation_;
    }

    bool ready() const { return hasOffset_; }

    // The peer's time to show at local time `nowMs`.
    uint32_t target(uint32_t nowMs) const { return nowMs - static_cast<uint32_t>(offset_) - kRenderDelayMs; }

    // Changes whenever the clock is cleared or re-anchored: a stream that last saw another one starts over.
    uint32_t generation() const { return generation_; }

private:
    void setOffset(int32_t transit) {
        offset_ = transit;
        hasOffset_ = true;
        laterSamples_ = 0;
    }

    bool hasOffset_ = false;
    int32_t offset_ = 0;
    uint32_t laterSamples_ = 0;
    uint32_t generation_ = 0;
};

}  // namespace stream_clock
