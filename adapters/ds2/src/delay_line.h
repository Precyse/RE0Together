#pragma once
// A timestamped interpolation buffer for one remote stream (pure logic, unit tested): reports go in with the time the
// sender stamped them (on our clock), and `at(now)` answers with the two reports around `now - delay`, so the remote
// is drawn a little in the past, between two real samples, instead of guessed ahead from the last one. The delay
// follows the stream's jitter (AdaptiveDelay). The caller interpolates its own value type between the two.
#include <algorithm>
#include <deque>
#include <optional>

#include "adaptive_delay.h"
#include "time_us.h"

template <class T>
class DelayLine {
public:
    static constexpr size_t kMaxSamples = 128;       // two seconds of a 60 Hz stream
    static constexpr TimeUs kRetainUs = 1'000'000;   // history kept behind the render time

    struct Bracket {
        const T* from;  // the report at or before the render time
        const T* to;    // the one after it; the same as `from` when only one side is known
        float alpha;    // 0 = from, 1 = to
        bool starved;   // the render time is past the newest report: nothing newer has arrived, `from` is held
    };

    // `sentLocalUs`: the sender's timestamp converted to our clock; `arrivedUs`: now. A report with a timestamp already
    // held replaces it (a duplicate).
    void push(TimeUs sentLocalUs, TimeUs arrivedUs, const T& value) {
        delay_.onArrival(sentLocalUs, arrivedUs);
        const auto position = std::lower_bound(samples_.begin(), samples_.end(), sentLocalUs,
                                               [](const Sample& s, TimeUs t) { return s.sentUs < t; });
        if (position != samples_.end() && position->sentUs == sentLocalUs) {
            position->value = value;
        } else {
            samples_.insert(position, Sample{sentLocalUs, value});
        }
        const TimeUs oldest = arrivedUs - delay_.delayUs() - kRetainUs;
        while (samples_.size() > 2 && samples_[1].sentUs < oldest) samples_.pop_front();
        while (samples_.size() > kMaxSamples) samples_.pop_front();
    }

    TimeUs delayUs() const { return delay_.delayUs(); }
    bool empty() const { return samples_.empty(); }
    void clear() {
        samples_.clear();
        delay_.reset();
    }

    // The reports around `now - delay`; nothing while empty. Before the oldest report the oldest is held. The pointers stay
    // valid until the next push.
    std::optional<Bracket> at(TimeUs now) const {
        if (samples_.empty()) return std::nullopt;
        const TimeUs render = now - delay_.delayUs();
        const Sample& first = samples_.front();
        const Sample& last = samples_.back();
        if (render <= first.sentUs) return Bracket{&first.value, &first.value, 0.0f, false};
        if (render >= last.sentUs) return Bracket{&last.value, &last.value, 0.0f, render > last.sentUs};
        const auto after = std::upper_bound(samples_.begin(), samples_.end(), render,
                                            [](TimeUs t, const Sample& s) { return t < s.sentUs; });
        const Sample& before = *(after - 1);
        const float alpha = static_cast<float>(render - before.sentUs) / static_cast<float>(after->sentUs - before.sentUs);
        return Bracket{&before.value, &after->value, alpha, false};
    }

private:
    struct Sample {
        TimeUs sentUs;
        T value;
    };

    AdaptiveDelay delay_;
    std::deque<Sample> samples_;
};
