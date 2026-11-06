#include "world_clock.h"

#include <optional>

#include "calls.h"
#include "clock_rule.h"
#include "log.h"
#include "refw.h"
#include "session.h"
#include "wire_dd2.h"
#include "world_gate.h"

namespace {

constexpr const char* kTimeManager = "app.TimeManager";
constexpr uint64_t kSendPeriodMs = 5000;
// TimeManager.setTime(hour, minute, day, weather immediately), from its decompile (docs/codemap/dd2.md "setTime"). The day
// is an absolute day only when it is ahead of the clock; kKeepDay leaves the day alone.
constexpr int32_t kKeepDay = -1;
constexpr bool kWeatherImmediate = true;  // as every engine caller passes it

clock_rule::Units g_units;
bool g_enabled = false;

uint32_t g_serial = 0;
uint64_t g_lastSendMs = 0;
int32_t g_lastSentHour = -1;

std::optional<wire_dd2::Clock> g_pending;  // guest: the host's last CLOCK not yet applied
std::optional<uint16_t> g_hostSerialAtLastSet;

void* asArg(int64_t value) { return reinterpret_cast<void*>(static_cast<intptr_t>(value)); }

std::optional<clock_rule::Time> ownTime(float* scale = nullptr) {
    void* manager = refw::managedSingleton(kTimeManager);
    if (!manager) return std::nullopt;
    const refw::InvokeResult* day = refw::invoke(calls::method(calls::kGetInGameDay), manager);
    if (!day) return std::nullopt;
    clock_rule::Time time;
    time.day = static_cast<int32_t>(day->dword);
    const refw::InvokeResult* seconds = refw::invoke(calls::method(calls::kGetInGameElapsedDaySeconds), manager);
    if (!seconds) return std::nullopt;
    time.daySeconds = seconds->f;
    if (scale) {
        const refw::InvokeResult* timeScale = refw::invoke(calls::method(calls::kGetTimeScale), manager);
        *scale = timeScale ? timeScale->f : 0.0f;
    }
    return time;
}

void hostTick(uint64_t nowMs) {
    float scale = 0.0f;
    const auto time = ownTime(&scale);
    if (!time) return;
    const int32_t hour = static_cast<int32_t>(time->daySeconds) / g_units.hourSeconds;
    if (nowMs - g_lastSendMs < kSendPeriodMs && hour == g_lastSentHour) return;
    wire_dd2::Clock clock{};
    clock.elapsedDaySeconds = time->daySeconds;
    clock.day = static_cast<uint32_t>(time->day);
    clock.timeScale = scale;
    clock.serial = ++g_serial;
    session::sendMessage(wire_dd2::kClock, true, clock);
    g_lastSendMs = nowMs;
    g_lastSentHour = hour;
}

void applyPending() {
    if (!g_pending || world_gate::place() != world_gate_rule::Place::InGame) return;
    const auto own = ownTime();
    void* manager = refw::managedSingleton(kTimeManager);
    if (!own || !manager) return;
    const auto hostPlace = session::hostPlace();
    const bool hostLoaded = hostPlace && g_hostSerialAtLastSet != hostPlace->loadSerial;
    const clock_rule::Time host{static_cast<int32_t>(g_pending->day), g_pending->elapsedDaySeconds};
    const clock_rule::Decision decision = clock_rule::decide(host, *own, g_units, hostLoaded);
    g_pending.reset();
    if (decision.verdict == clock_rule::Verdict::RefuseBackwards) {
        logger::writeUnlessRepeated("world_clock: ahead of the host by %.0f game s, kept (no host load since)", -decision.offGameSeconds);
        return;
    }
    if (decision.verdict != clock_rule::Verdict::Set) return;
    const int32_t day = decision.day >= own->day ? decision.day : kKeepDay;
    const bool ok = refw::invoke(calls::method(calls::kSetTime), manager,
                                 {asArg(decision.hour), asArg(decision.minute), asArg(day), asArg(kWeatherImmediate)}) != nullptr;
    if (hostPlace) g_hostSerialAtLastSet = hostPlace->loadSerial;
    logger::write("world_clock: %.0f game s off the host: set day %d %02d:%02d (%s)", decision.offGameSeconds, day,
                  decision.hour, decision.minute, ok ? "ok" : "setTime failed");
}

}  // namespace

namespace world_clock {

void install() {
    if (!calls::enabled(calls::Feature::Clock)) return;
    refw::staticField(kTimeManager, "InGameDaySeconds", g_units.daySeconds);
    refw::staticField(kTimeManager, "InGameHourSeconds", g_units.hourSeconds);
    refw::staticField(kTimeManager, "InGameMinuteSeconds", g_units.minuteSeconds);
    g_enabled = g_units.valid();
    logger::write("world_clock: day %d s, hour %d s, minute %d s; %s", g_units.daySeconds, g_units.hourSeconds,
                  g_units.minuteSeconds, g_enabled ? "host side on, guest side on" : "off (clock units not read)");
}

void tick(uint64_t nowMs) {
    if (!g_enabled) return;
    if (session::isHost()) {
        if (world_gate::place() == world_gate_rule::Place::InGame) hostTick(nowMs);
    } else {
        applyPending();
    }
}

bool onClock(std::span<const uint8_t> payload, uint64_t) {
    const auto clock = wire_dd2::decodeClock(payload);
    if (!clock) return false;
    g_pending = *clock;
    return true;
}

}  // namespace world_clock
