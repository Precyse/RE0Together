#pragma once
// The guest's world clock follows the host's (pure, unit tested; DD2 design world.md 7). The guest's clock keeps
// running so its local systems tick; it is set only when it is more than 60 game seconds off, and never backwards
// unless the host loaded since the last set (one writer per clock: the host).
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace clock_rule {

constexpr double kToleranceGameSeconds = 60.0;

// The game's own units, read from TimeManager's static fields at start.
struct Units {
    int32_t daySeconds = 0;
    int32_t hourSeconds = 0;
    int32_t minuteSeconds = 0;
    bool valid() const { return daySeconds > 0 && hourSeconds > 0 && minuteSeconds > 0 && hourSeconds <= daySeconds; }
};

struct Time {
    int32_t day = 0;
    double daySeconds = 0.0;  // seconds since the start of the day
};

enum class Verdict : uint8_t { Keep, Set, RefuseBackwards, BadUnits };

struct Decision {
    Verdict verdict = Verdict::Keep;
    double offGameSeconds = 0.0;  // host minus guest
    int32_t day = 0, hour = 0, minute = 0;  // what to set, for Verdict::Set
};

inline Decision decide(const Time& host, const Time& guest, const Units& units, bool hostLoadedSinceLastSet) {
    Decision d;
    if (!units.valid()) {
        d.verdict = Verdict::BadUnits;
        return d;
    }
    d.offGameSeconds = static_cast<double>(host.day - guest.day) * units.daySeconds + (host.daySeconds - guest.daySeconds);
    if (std::fabs(d.offGameSeconds) <= kToleranceGameSeconds) return d;
    if (d.offGameSeconds < 0 && !hostLoadedSinceLastSet) {
        d.verdict = Verdict::RefuseBackwards;
        return d;
    }
    const auto seconds = static_cast<int32_t>(host.daySeconds);
    d.verdict = Verdict::Set;
    d.day = host.day;
    d.hour = seconds / units.hourSeconds;
    d.minute = (seconds % units.hourSeconds) / units.minuteSeconds;
    return d;
}

}  // namespace clock_rule
