#include "clock_sync.h"

#include <cstring>
#include <map>
#include <mutex>
#include <set>

#include "clock_offset.h"
#include "reject_counters.h"

namespace {

constexpr TimeUs kWarmupPingIntervalUs = 250'000;  // until the estimate has a full set of samples
constexpr TimeUs kSteadyPingIntervalUs = 2'000'000;

struct PeerClock {
    ClockOffset offset;
    TimeUs lastPingUs = 0;
};

std::mutex g_mutex;  // the net thread feeds the estimates, any thread reads them
std::map<uint8_t, PeerClock> g_peers;

TimeUs pingInterval(const ClockOffset& offset) {
    return offset.samples() < ClockOffset::kSamples ? kWarmupPingIntervalUs : kSteadyPingIntervalUs;
}

}  // namespace

namespace clock_sync {

void onFrame(NetClient& net, const GameFrame& frame) {
    if (frame.type == kMsgClockPing && frame.payload.size() == sizeof(ClockPing)) {
        ClockPing ping;
        std::memcpy(&ping, frame.payload.data(), sizeof(ping));
        const ClockPong pong{ping.sentUs, static_cast<uint64_t>(nowUs())};
        net.send(kMsgClockPong, false, frame.slot, proto::bytesOf(pong));
    } else if (frame.type == kMsgClockPong && frame.payload.size() == sizeof(ClockPong)) {
        const TimeUs receivedUs = nowUs();
        ClockPong pong;
        std::memcpy(&pong, frame.payload.data(), sizeof(pong));
        std::lock_guard lock(g_mutex);
        const auto peer = g_peers.find(frame.slot);
        if (peer == g_peers.end()) return;
        if (!peer->second.offset.addSample(static_cast<TimeUs>(pong.pingSentUs), receivedUs,
                                           static_cast<TimeUs>(pong.peerUs))) {
            reject_counters::count(kMsgClockPong, reject_counters::Reason::StaleEpoch);
        }
    } else if (frame.type == kMsgClockPing || frame.type == kMsgClockPong) {
        reject_counters::count(frame.type, reject_counters::Reason::Malformed);
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const TimeUs now = nowUs();
    std::set<uint8_t> due;
    {
        std::lock_guard lock(g_mutex);
        std::set<uint8_t> present;
        for (const PeerInfo& info : session.peers) present.insert(info.slot);
        std::erase_if(g_peers, [&](const auto& entry) { return !session.linked || !present.contains(entry.first); });
        if (!session.linked) return;
        for (const uint8_t slot : present) {
            PeerClock& peer = g_peers[slot];
            if (now - peer.lastPingUs < pingInterval(peer.offset)) continue;
            peer.lastPingUs = now;
            due.insert(slot);
        }
    }
    for (const uint8_t slot : due) net.send(kMsgClockPing, false, slot, proto::bytesOf(ClockPing{static_cast<uint64_t>(now)}));
}

std::optional<TimeUs> toLocalUs(uint8_t slot, TimeUs peerUs) {
    std::lock_guard lock(g_mutex);
    const auto peer = g_peers.find(slot);
    if (peer == g_peers.end() || !peer->second.offset.valid()) return std::nullopt;
    return peer->second.offset.toLocal(peerUs);
}

std::optional<TimeUs> rttUs(uint8_t slot) {
    std::lock_guard lock(g_mutex);
    const auto peer = g_peers.find(slot);
    if (peer == g_peers.end() || !peer->second.offset.valid()) return std::nullopt;
    return peer->second.offset.rttUs();
}

}  // namespace clock_sync
