#pragma once
// The game's time of day, pa::FieldTime, and the guest's catch-up rule (pure, unit tested; cd design world.md 12).
//
// "Raw" time is what advances: a day is 75,600,000 raw units and the six night hours (21:00-03:00) take half as many
// raw units as their length in milliseconds, so night passes twice as fast (toRaw / fromRaw of the build). The game
// carries the stored value forward as raw + (now - anchor) * scale, "now" on the server clock (scale 12 by default).
// The guest sets its own server's time with SetFieldTimeReq: the host's hour on the guest's nearest day (its day count
// never jumps by more than half a day), only when it is more than about two game minutes off, at most once per 15 s
// and 8 times per 5 minutes.
#include <cstdint>
#include <cstring>
#include <deque>
#include <optional>
#include <vector>

namespace field_time_rule {

constexpr uint64_t kRawDay = 75'600'000;
constexpr uint64_t kNightEndMs = 10'800'000;    // 03:00
constexpr uint64_t kNightStartMs = 75'600'000;  // 21:00
constexpr uint64_t kNightShift = 5'400'000;
constexpr uint64_t kNightWrap = 64'800'000;
constexpr uint32_t kMsPerSecond = 1000, kSecondsPerMinute = 60, kMinutesPerHour = 60, kHoursPerDay = 24, kDaysPerWeek = 7;
constexpr uint16_t kMaxScale = 1000;
constexpr size_t kMemorySize = 32;  // in the game's memory (a pad byte at +0x15)
constexpr size_t kWireSize = 31;    // serialised (no pad)
constexpr size_t kSetReqSize = kWireSize + 1 + 4;  // SetFieldTimeReq: FieldTime, u8 flag 0, u32 key 0

constexpr uint64_t kCatchUpRaw = 120'000;      // about two game minutes
constexpr uint64_t kMinGapMs = 15'000;
constexpr uint64_t kBurstWindowMs = 300'000;
constexpr size_t kBurstMax = 8;

struct FieldTime {
    uint32_t day = 0, hour = 0, minute = 0, second = 0, ms = 0;
    uint8_t weekday = 0;
    uint16_t scale = 0;
    uint64_t anchor = 0;  // server clock (ms) when the value was set; 0 on fixed-time levels

    uint64_t toRaw() const {
        const uint64_t t = ((static_cast<uint64_t>(hour) * kMinutesPerHour + minute) * kSecondsPerMinute + second) * kMsPerSecond + ms;
        const uint64_t base = static_cast<uint64_t>(day) * kRawDay;
        if (t < kNightEndMs) return base + t / 2;
        if (t < kNightStartMs) return base + t - kNightShift;
        return base + (t + kNightWrap) / 2;
    }

    // Sets the date and time from raw; scale and anchor stay.
    void setRaw(uint64_t raw) {
        const uint64_t d = raw / kRawDay;
        const uint64_t r = raw - d * kRawDay;
        const uint64_t t = r < kNightShift ? 2 * r : r < kRawDay - kNightShift ? r + kNightShift : 2 * r - kNightWrap;
        day = static_cast<uint32_t>(d);
        weekday = static_cast<uint8_t>(day % kDaysPerWeek);
        ms = static_cast<uint32_t>(t % kMsPerSecond);
        second = static_cast<uint32_t>(t / kMsPerSecond % kSecondsPerMinute);
        minute = static_cast<uint32_t>(t / (kMsPerSecond * kSecondsPerMinute) % kMinutesPerHour);
        hour = static_cast<uint32_t>(t / (kMsPerSecond * kSecondsPerMinute * kMinutesPerHour) % kHoursPerDay);
    }

    // The stored value carried forward to the server clock `now`.
    uint64_t rawAt(uint64_t now) const { return toRaw() + (now > anchor ? (now - anchor) * scale : 0); }

    bool fixed() const { return anchor == 0; }
};

inline std::optional<FieldTime> fromMemory(const uint8_t* p) {
    FieldTime f;
    std::memcpy(&f.day, p + 0x00, 4);
    std::memcpy(&f.hour, p + 0x04, 4);
    std::memcpy(&f.minute, p + 0x08, 4);
    std::memcpy(&f.second, p + 0x0C, 4);
    std::memcpy(&f.ms, p + 0x10, 4);
    f.weekday = p[0x14];
    std::memcpy(&f.scale, p + 0x16, 2);
    std::memcpy(&f.anchor, p + 0x18, 8);
    if (f.hour >= kHoursPerDay || f.minute >= kMinutesPerHour || f.second >= kSecondsPerMinute || f.ms >= kMsPerSecond ||
        f.scale == 0 || f.scale > kMaxScale) {
        return std::nullopt;
    }
    return f;
}

// SetFieldTimeReq payload: the FieldTime serialised, then flag 0 (only set the time) and key 0.
inline std::vector<uint8_t> setReq(const FieldTime& f) {
    std::vector<uint8_t> out(kSetReqSize, 0);
    std::memcpy(out.data() + 0, &f.day, 4);
    std::memcpy(out.data() + 4, &f.hour, 4);
    std::memcpy(out.data() + 8, &f.minute, 4);
    std::memcpy(out.data() + 12, &f.second, 4);
    std::memcpy(out.data() + 16, &f.ms, 4);
    out[20] = f.weekday;
    std::memcpy(out.data() + 21, &f.scale, 2);
    std::memcpy(out.data() + 23, &f.anchor, 8);
    return out;
}

// The raw time that gives `ours` the time of day of `theirs` on the nearest day.
inline uint64_t nearestWithTimeOf(uint64_t ours, uint64_t theirs) {
    const uint64_t timeOfDay = theirs % kRawDay;
    uint64_t target = ours / kRawDay * kRawDay + timeOfDay;
    if (target + kRawDay / 2 < ours) {
        target += kRawDay;
    } else if (target > ours + kRawDay / 2 && target >= kRawDay) {
        target -= kRawDay;
    }
    return target;
}

// The guest's limiter: when a catch-up may be posted.
class CatchUp {
public:
    // The raw to set now, or null (close enough, too soon, or too many lately).
    std::optional<uint64_t> decide(uint64_t ours, uint64_t hosts, uint64_t nowMs) {
        const uint64_t target = nearestWithTimeOf(ours, hosts);
        const uint64_t gap = target > ours ? target - ours : ours - target;
        if (gap <= kCatchUpRaw) return std::nullopt;
        while (!sent_.empty() && nowMs - sent_.front() >= kBurstWindowMs) sent_.pop_front();
        if (!sent_.empty() && nowMs - sent_.back() < kMinGapMs) return std::nullopt;
        if (sent_.size() >= kBurstMax) return std::nullopt;
        sent_.push_back(nowMs);
        return target;
    }

private:
    std::deque<uint64_t> sent_;
};

}  // namespace field_time_rule
