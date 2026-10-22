#include "crater_sync.h"

#include <chrono>
#include <vector>

#include "crater_wire.h"
#include "ds2/craters.h"
#include "log.h"
#include "peer_joins.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kRepeatInterval = std::chrono::seconds(5);  // the host repeats an unchanged set this often

// Net thread only.
bool g_guest = false;
uint8_t g_hostSlot = 0;
PeerJoins g_joins;  // host: which peer joins have been sent the set
Clock::time_point g_lastSent;
std::vector<crater_wire::Crater> g_sent;
bool g_sentOnce = false;

bool send(NetClient& net, uint8_t destSlot, const std::vector<crater_wire::Crater>& set) {
    return net.send(crater_wire::kMsgCraterSet, true, destSlot, crater_wire::encode(set));
}

void sendSet(NetClient& net, const SessionSnapshot& session) {
    std::vector<crater_wire::Crater> set;
    if (!craters::hostSet(set)) return;
    for (const uint8_t slot : g_joins.takeNew(session)) {
        if (send(net, slot, set)) logger::write("crater_sync: sent %zu craters to the joining slot %u", set.size(), slot);
    }
    const auto now = Clock::now();
    const bool changed = !g_sentOnce || !crater_wire::sameSet(set, g_sent);
    if (!changed && now - g_lastSent < kRepeatInterval) return;
    if (!send(net, proto::kSlotAll, set)) return;
    if (changed) logger::write("crater_sync: craters now %zu", set.size());
    g_sent = std::move(set);
    g_sentOnce = true;
    g_lastSent = now;
}

}  // namespace

namespace crater_sync {

void onFrame(const GameFrame& frame) {
    if (!g_guest || frame.slot != g_hostSlot || frame.type != crater_wire::kMsgCraterSet) return;
    std::vector<crater_wire::Crater> set;
    if (crater_wire::decode(frame.payload, set)) {
        craters::follow(std::move(set));
    } else {
        logger::write("crater_sync: dropped a malformed CRATER_SET (%zu bytes)", frame.payload.size());
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const bool host = session.linked && session.localSlot == session.hostSlot;
    g_guest = session.linked && !host;
    g_hostSlot = session.hostSlot;
    craters::setRole(host, g_guest);
    if (!host) {
        g_sentOnce = false;
        g_joins.clear();
        return;
    }
    sendSet(net, session);
}

}  // namespace crater_sync
