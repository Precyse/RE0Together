#include "debug_stats.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace {

using Clock = std::chrono::steady_clock;
using debug_stats::kCounterCount;
using debug_stats::kGaugeCount;

constexpr auto kRateWindow = std::chrono::milliseconds(1000);
constexpr size_t kErrorCapacity = 160;
constexpr long long kMsPerSecond = 1000;

std::array<std::atomic<uint32_t>, kCounterCount> g_total{};
// Order of debug_stats::Gauge: PadDepth, BillyOwner, RebeccaOwner, SaveRedirect, WorldFrozen, FloorPending, PartyMode,
// FocusedCharacter, DoorPhase, Room.
std::array<std::atomic<int>, kGaugeCount> g_gauge{0, debug_stats::kUnknownOwner, debug_stats::kUnknownOwner, 0, 0, 0, 0,
                                                  debug_stats::kUnknownOwner, 0, 0};

std::atomic<bool> g_linked{false};
std::atomic<uint8_t> g_localSlot{0};
std::atomic<uint8_t> g_hostSlot{0};
std::atomic<uint32_t> g_epoch{0};

std::mutex g_mutex;  // guards everything below
std::vector<PeerInfo> g_peers;
std::vector<std::string> g_disabled;
std::string g_lastError;
Clock::time_point g_errorTime;
std::string g_lastCommand;
Clock::time_point g_commandTime;
std::string g_lastDecision;
Clock::time_point g_decisionTime;
Clock::time_point g_windowStart = Clock::now();
std::array<uint32_t, kCounterCount> g_windowBase{};
std::array<uint32_t, kCounterCount> g_rate{};

uint64_t ageMs(Clock::time_point since, Clock::time_point now) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - since).count());
}

}  // namespace

namespace debug_stats {

void count(Counter counter, uint32_t amount) {
    g_total[static_cast<size_t>(counter)].fetch_add(amount, std::memory_order_relaxed);
}

void set(Gauge gauge, int value) { g_gauge[static_cast<size_t>(gauge)].store(value, std::memory_order_relaxed); }

void setSession(const SessionSnapshot& session) {
    g_linked = session.linked;
    g_localSlot = session.localSlot;
    g_hostSlot = session.hostSlot;
    g_epoch = session.epoch;
    std::lock_guard lock(g_mutex);
    g_peers = session.peers;
}

void noteCallbackDisabled(const char* name) {
    std::lock_guard lock(g_mutex);
    g_disabled.emplace_back(name);
}

void setError(const char* format, ...) {
    char text[kErrorCapacity];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    std::lock_guard lock(g_mutex);
    g_lastError = text;
    g_errorTime = Clock::now();
}

void setLastCommand(const char* text) {
    std::lock_guard lock(g_mutex);
    g_lastCommand = text;
    g_commandTime = Clock::now();
}

void setLastDecision(const char* text) {
    std::lock_guard lock(g_mutex);
    g_lastDecision = text;
    g_decisionTime = Clock::now();
}

Snapshot snapshot() {
    Snapshot out;
    out.linked = g_linked;
    out.localSlot = g_localSlot;
    out.hostSlot = g_hostSlot;
    out.epoch = g_epoch;
    for (size_t i = 0; i < kCounterCount; ++i) out.total[i] = g_total[i].load(std::memory_order_relaxed);
    for (size_t i = 0; i < kGaugeCount; ++i) out.gauge[i] = g_gauge[i].load(std::memory_order_relaxed);

    std::lock_guard lock(g_mutex);
    const auto now = Clock::now();
    const auto elapsed = now - g_windowStart;
    if (elapsed >= kRateWindow) {
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        for (size_t i = 0; i < kCounterCount; ++i) {
            g_rate[i] = static_cast<uint32_t>((out.total[i] - g_windowBase[i]) * kMsPerSecond / ms);
        }
        g_windowBase = out.total;
        g_windowStart = now;
    }
    out.perSecond = g_rate;
    out.peers = g_peers;
    out.disabledCallbacks = g_disabled;
    out.lastError = g_lastError;
    if (!g_lastError.empty()) {
        out.lastErrorAgeMs = ageMs(g_errorTime, now);
    }
    out.lastCommand = {g_lastCommand, ageMs(g_commandTime, now)};
    out.lastDecision = {g_lastDecision, ageMs(g_decisionTime, now)};
    return out;
}

}  // namespace debug_stats
