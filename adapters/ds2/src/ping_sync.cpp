#include "ping_sync.h"

#include <map>
#include <mutex>

#include "ds2/user_markers.h"
#include "log.h"
#include "peer_joins.h"
#include "reject_counters.h"

namespace {

std::mutex g_mutex;
std::map<uint8_t, std::vector<ping_wire::Ping>> g_partners;  // by slot; the newest set of each (guarded by g_mutex)

// Net thread only.
PeerJoins g_joins;  // which peer joins have been sent the current set

bool send(NetClient& net, uint8_t destSlot, const std::vector<ping_wire::Ping>& set) {
    return net.send(ping_wire::kMsgPingSet, true, destSlot, ping_wire::encode(set));
}

// A peer that left or a lost link takes its pings with it.
void forgetDeparted(const SessionSnapshot& session) {
    std::lock_guard lock(g_mutex);
    std::erase_if(g_partners, [&](const auto& entry) {
        if (!session.linked) return true;
        for (const PeerInfo& peer : session.peers) {
            if (peer.slot == entry.first) return false;
        }
        return true;
    });
}

}  // namespace

namespace ping_sync {

void onFrame(const GameFrame& frame) {
    if (frame.type != ping_wire::kMsgPingSet) return;
    std::vector<ping_wire::Ping> set;
    if (!ping_wire::decode(frame.payload, set)) {
        reject_counters::count(frame.type, reject_counters::Reason::Malformed);
        logger::write("ping_sync: dropped a malformed PING_SET (%zu bytes)", frame.payload.size());
        return;
    }
    std::lock_guard lock(g_mutex);
    if (set.empty()) {
        g_partners.erase(frame.slot);
    } else {
        g_partners[frame.slot] = std::move(set);
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    forgetDeparted(session);
    if (!session.linked) {
        g_joins.clear();
        return;
    }
    std::vector<ping_wire::Ping> set;
    const bool changed = user_markers::takeChangedSet(set);
    if (changed && send(net, proto::kSlotAll, set)) logger::write("ping_sync: %zu pings placed", set.size());
    const std::vector<uint8_t> joined = g_joins.takeNew(session);
    if (joined.empty() || !user_markers::currentSet(set) || set.empty()) return;
    for (const uint8_t slot : joined) {
        if (!changed && send(net, slot, set)) logger::write("ping_sync: sent %zu pings to the joining slot %u", set.size(), slot);
    }
}

std::vector<PartnerPing> partnerPings() {
    std::vector<PartnerPing> out;
    std::lock_guard lock(g_mutex);
    for (const auto& [slot, set] : g_partners) {
        for (const ping_wire::Ping& ping : set) out.push_back({slot, ping});
    }
    return out;
}

}  // namespace ping_sync
