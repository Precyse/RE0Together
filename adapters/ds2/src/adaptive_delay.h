#pragma once
// How far behind real time a remote stream is rendered (pure logic, unit tested). The delay is as small as the link
// allows: it grows at once when reports start arriving late and shrinks only slowly after a calm stretch. Lateness is
// each report's transit time (arrival minus the sender's timestamp on our clock) above the fastest transit lately, so
// a constant clock error cancels out.
#include <algorithm>

#include "time_us.h"

class AdaptiveDelay {
public:
    static constexpr TimeUs kMinDelayUs = 150'000;
    static constexpr TimeUs kMaxDelayUs = 300'000;
    static constexpr int kJitterMargin = 2;                   // delay = minimum + margin x the worst lateness seen
    static constexpr TimeUs kEvaluationUs = 3'000'000;        // how often the delay may shrink
    static constexpr TimeUs kShrinkStepUs = 10'000;           // by this much per evaluation
    static constexpr TimeUs kBaselineLeakUsPerSecond = 1000;  // the fastest transit creeps up, so a slower route is followed

    TimeUs delayUs() const { return delay_; }

    // One report: when the sender stamped it (already on our clock) and when it arrived.
    void onArrival(TimeUs sentLocalUs, TimeUs arrivedUs) {
        const TimeUs transit = arrivedUs - sentLocalUs;
        if (!started_) {
            started_ = true;
            baseline_ = transit;
            lastArrival_ = windowStart_ = arrivedUs;
        }
        baseline_ += (arrivedUs - lastArrival_) * kBaselineLeakUsPerSecond / 1'000'000;
        lastArrival_ = arrivedUs;
        baseline_ = std::min(baseline_, transit);
        windowPeak_ = std::max(windowPeak_, transit - baseline_);
        delay_ = std::max(delay_, wanted(windowPeak_));
        if (arrivedUs - windowStart_ < kEvaluationUs) return;
        delay_ = std::max(wanted(windowPeak_), delay_ - kShrinkStepUs);
        windowStart_ = arrivedUs;
        windowPeak_ = 0;
    }

    void reset() { *this = AdaptiveDelay(); }

private:
    static TimeUs wanted(TimeUs lateness) {
        return std::clamp(kMinDelayUs + kJitterMargin * lateness, kMinDelayUs, kMaxDelayUs);
    }

    TimeUs delay_ = kMinDelayUs;
    TimeUs baseline_ = 0;  // the fastest recent transit
    TimeUs windowPeak_ = 0;  // the worst lateness since the last evaluation
    TimeUs windowStart_ = 0;
    TimeUs lastArrival_ = 0;
    bool started_ = false;
};
