#include "world_clock.h"

#include <array>
#include <cctype>
#include <optional>
#include <string>

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
constexpr size_t kSetTimeInts = 3;

clock_rule::Units g_units;
// setTime's argument position of day, hour and minute, from its parameter names; unset when they did not match.
std::optional<std::array<size_t, kSetTimeInts>> g_setTimeOrder;
bool g_enabled = false;

uint32_t g_serial = 0;
uint64_t g_lastSendMs = 0;
int32_t g_lastSentHour = -1;

std::optional<wire_dd2::Clock> g_pending;  // guest: the host's last CLOCK not yet applied
std::optional<uint16_t> g_hostSerialAtLastSet;

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

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// Day, hour and minute positions among setTime's parameters, matched by name.
std::optional<std::array<size_t, kSetTimeInts>> setTimeOrder() {
    const std::vector<std::string> names = refw::paramNames(calls::method(calls::kSetTime));
    std::string joined;
    for (const std::string& name : names) joined += name + " ";
    logger::write("world_clock: TimeManager.setTime parameters: %s", joined.c_str());
    constexpr std::array<const char*, kSetTimeInts> kWanted{"day", "hour", "min"};
    std::array<size_t, kSetTimeInts> order{};
    for (size_t want = 0; want < kSetTimeInts; ++want) {
        size_t found = names.size();
        for (size_t i = 0; i < kSetTimeInts && i < names.size(); ++i) {
            if (lower(names[i]).find(kWanted[want]) != std::string::npos) found = i;
        }
        if (found == names.size()) return std::nullopt;
        order[want] = found;
    }
    return order;
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
    if (!g_pending || !g_setTimeOrder || world_gate::place() != world_gate_rule::Place::InGame) return;
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
    std::array<void*, kSetTimeInts + 1> args{};
    const int32_t values[kSetTimeInts] = {decision.day, decision.hour, decision.minute};
    for (size_t i = 0; i < kSetTimeInts; ++i) args[(*g_setTimeOrder)[i]] = reinterpret_cast<void*>(static_cast<intptr_t>(values[i]));
    const bool ok = refw::invoke(calls::method(calls::kSetTime), manager, {args[0], args[1], args[2], args[3]}) != nullptr;
    if (hostPlace) g_hostSerialAtLastSet = hostPlace->loadSerial;
    logger::write("world_clock: %.0f game s off the host: set day %d %02d:%02d (%s)", decision.offGameSeconds, decision.day,
                  decision.hour, decision.minute, ok ? "ok" : "setTime failed");
}

}  // namespace

namespace world_clock {

void install() {
    if (!calls::enabled(calls::Feature::Clock)) return;
    refw::staticField(kTimeManager, "InGameDaySeconds", g_units.daySeconds);
    refw::staticField(kTimeManager, "InGameHourSeconds", g_units.hourSeconds);
    refw::staticField(kTimeManager, "InGameMinuteSeconds", g_units.minuteSeconds);
    g_setTimeOrder = setTimeOrder();
    g_enabled = g_units.valid();
    logger::write("world_clock: day %d s, hour %d s, minute %d s; host side %s, guest side %s", g_units.daySeconds,
                  g_units.hourSeconds, g_units.minuteSeconds, g_enabled ? "on" : "off",
                  g_enabled && g_setTimeOrder ? "on" : "off (setTime parameters not recognised)");
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
