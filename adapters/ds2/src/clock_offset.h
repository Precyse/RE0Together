#pragma once
// The offset between a peer's clock and this machine's, from ping/pong exchanges (pure logic, unit tested). A ping
// leaves at local time t0, the peer stamps its own time `peer` when it answers, the pong arrives at local time t3.
// Taking the peer's stamp as the middle of the round trip, the peer's clock reads `peer - (t0 + t3) / 2` ahead of ours.
// The estimate is the median of the last few samples, so one slow round trip does not move it.
#include <algorithm>
#include <array>
#include <cstddef>

#include "time_us.h"

class ClockOffset {
public:
    static constexpr size_t kSamples = 16;     // the median is taken over the newest of these
    static constexpr size_t kMinSamples = 3;   // fewer than this is not an estimate yet
    static constexpr TimeUs kMaxRttUs = 2'000'000;  // a slower round trip says nothing about the offset

    // False for a sample that cannot be right (negative or huge round trip).
    bool addSample(TimeUs sentLocalUs, TimeUs receivedLocalUs, TimeUs peerUs) {
        const TimeUs rtt = receivedLocalUs - sentLocalUs;
        if (rtt < 0 || rtt > kMaxRttUs) return false;
        const size_t at = count_ % kSamples;
        offsets_[at] = peerUs - (sentLocalUs + receivedLocalUs) / 2;
        rtts_[at] = rtt;
        ++count_;
        return true;
    }

    bool valid() const { return count_ >= kMinSamples; }
    size_t samples() const { return std::min(count_, kSamples); }

    // The peer's clock minus ours, microseconds.
    TimeUs offsetUs() const { return median(offsets_); }
    TimeUs rttUs() const { return median(rtts_); }

    // A time stamped by the peer, on our clock; and the other way round.
    TimeUs toLocal(TimeUs peerUs) const { return peerUs - offsetUs(); }
    TimeUs toPeer(TimeUs localUs) const { return localUs + offsetUs(); }

private:
    using Samples = std::array<TimeUs, kSamples>;

    TimeUs median(const Samples& values) const {
        const size_t n = samples();
        if (n == 0) return 0;
        Samples sorted = values;
        std::sort(sorted.begin(), sorted.begin() + n);
        return n % 2 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2;
    }

    Samples offsets_{};
    Samples rtts_{};
    size_t count_ = 0;
};
