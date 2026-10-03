#include "anim_sync.h"

#include <chrono>
#include <map>
#include <vector>

#include "adaptive_delay.h"
#include "anim_wire.h"
#include "clock_sync.h"
#include "ds2/remote_animation.h"
#include "log.h"
#include "pulse_detector.h"
#include "reject_counters.h"
#include "resync.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kSendInterval = std::chrono::milliseconds(33);
constexpr auto kSnapshotInterval = std::chrono::seconds(1);
constexpr size_t kMaxPulsesPerReport = 8;  // a boolean that flaps cannot flood the reliable channel

Clock::time_point g_lastSend;
Clock::time_point g_lastSnapshot;
uint32_t g_seq = 0;
constexpr auto kStatsInterval = std::chrono::seconds(5);
Clock::time_point g_lastStats;
uint64_t g_bytesSent = 0;
uint32_t g_reportsSent = 0;
uint32_t g_pulsesSent = 0;
PulseDetector g_pulses;
std::map<uint8_t, AdaptiveDelay> g_peerDelay;  // per partner: how late its reports arrive (not yet used to delay them)

void logStats(Clock::time_point now) {
    const double seconds = std::chrono::duration<double>(now - g_lastStats).count();
    logger::write("anim_sync: sent %u reports and %u pulses, %.0f bytes/s in the last %.0f s", g_reportsSent, g_pulsesSent,
                  g_bytesSent / seconds, seconds);
    for (const auto& [slot, delay] : g_peerDelay) {
        const std::optional<TimeUs> rtt = clock_sync::rttUs(slot);
        logger::write("anim_sync: slot %u render delay %lld ms, round trip %lld ms", slot,
                      static_cast<long long>(delay.delayUs() / 1000), static_cast<long long>(rtt ? *rtt / 1000 : -1));
    }
    g_lastStats = now;
    g_bytesSent = 0;
    g_reportsSent = 0;
    g_pulsesSent = 0;
}

void countSent(Clock::time_point now, size_t bytes) {
    g_bytesSent += bytes;
    ++g_reportsSent;
    if (now - g_lastStats >= kStatsInterval) logStats(now);
}

// The boolean changes that ended a pulse in this report, as the pulse values.
std::vector<remote_animation::Change> pulsesIn(const std::vector<remote_animation::Change>& changes, TimeUs now) {
    std::vector<remote_animation::Change> pulses;
    for (const remote_animation::Change& change : changes) {
        if (change.type != remote_animation::kTypeBool) continue;
        const std::optional<bool> held = g_pulses.onBool(change.index, change.value[0] != 0, now);
        if (!held || pulses.size() >= kMaxPulsesPerReport) continue;
        remote_animation::Change pulse = change;
        pulse.value[0] = *held ? 1 : 0;
        pulses.push_back(pulse);
    }
    return pulses;
}

}  // namespace

namespace anim_sync {

void onFrame(const GameFrame& frame) {
    if (frame.type != anim_wire::kMsgAnimState) return;
    anim_wire::Report report;
    if (!anim_wire::decode(frame.payload, report)) {
        reject_counters::count(frame.type, reject_counters::Reason::Malformed);
        return;
    }
    for (const remote_animation::Change& change : report.changes) remote_animation::setPeerChange(frame.slot, change);
    if (!report.sentUs) return;
    if (const std::optional<TimeUs> sentLocal = clock_sync::toLocalUs(frame.slot, *report.sentUs)) {
        g_peerDelay[frame.slot].onArrival(*sentLocal, nowUs());
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const auto now = Clock::now();
    remote_animation::setCollecting(session.linked);
    if (!session.linked) {
        g_peerDelay.clear();
        return;
    }
    if (!resync::takeRequests(resync::kAnim).empty()) remote_animation::requestSnapshot();
    if (now - g_lastSend < kSendInterval) return;
    g_lastSend = now;
    const bool snapshot = now - g_lastSnapshot >= kSnapshotInterval;
    if (snapshot) {
        g_lastSnapshot = now;
        remote_animation::requestSnapshot();
    }
    const std::vector<remote_animation::Change> changes = remote_animation::takeLocalChanges();
    if (changes.empty()) return;
    const TimeUs sentUs = nowUs();
    const std::vector<uint8_t> payload = anim_wire::encode(++g_seq, snapshot, sentUs, changes);
    if (net.send(anim_wire::kMsgAnimState, false, proto::kSlotAll, payload)) countSent(now, payload.size());
    const std::vector<remote_animation::Change> pulses = pulsesIn(changes, sentUs);
    if (pulses.empty()) return;
    if (net.send(anim_wire::kMsgAnimEvent, true, proto::kSlotAll, anim_wire::encode(g_seq, false, sentUs, pulses))) {
        g_pulsesSent += static_cast<uint32_t>(pulses.size());
    }
}

}  // namespace anim_sync
