#include "bt_sync.h"

#include <chrono>

#include "bt_wire.h"
#include "ds2/bt_events.h"
#include "log.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kSendInterval = std::chrono::seconds(1);  // the host repeats the set this often (a joining guest needs no snapshot)
constexpr auto kCheckInterval = std::chrono::milliseconds(200);

// Net thread only.
bool g_guest = false;
uint8_t g_hostSlot = 0;
Clock::time_point g_lastCheck;
Clock::time_point g_lastSent;
bt_wire::BtEnv g_sent{};
bool g_sentOnce = false;

void sendRegions(NetClient& net) {
    const auto now = Clock::now();
    if (now - g_lastCheck < kCheckInterval) return;
    g_lastCheck = now;
    bt_wire::BtEnv env;
    if (!bt_events::readActiveRegions(env)) return;
    const bool changed = !g_sentOnce || env.activeRegions != g_sent.activeRegions;
    if (!changed && now - g_lastSent < kSendInterval) return;
    if (!net.send(bt_wire::kMsgBtEnv, true, proto::kSlotAll, proto::bytesOf(env))) return;
    if (changed) logger::write("bt_sync: BT regions now %016llx", static_cast<unsigned long long>(env.activeRegions));
    g_sent = env;
    g_sentOnce = true;
    g_lastSent = now;
}

}  // namespace

namespace bt_sync {

void onFrame(const GameFrame& frame) {
    if (!g_guest || frame.slot != g_hostSlot) return;
    if (frame.type == bt_wire::kMsgBtEnv) {
        bt_wire::BtEnv env;
        if (bt_wire::decode(frame.payload, env)) {
            bt_events::followRegions(env);
        } else {
            logger::write("bt_sync: dropped a malformed BT_ENV (%zu bytes)", frame.payload.size());
        }
    } else if (frame.type == bt_wire::kMsgCatcherEvent) {
        bt_wire::CatcherEvent event;
        if (bt_wire::decode(frame.payload, event)) {
            bt_events::replayCatcher(event);
        } else {
            logger::write("bt_sync: dropped a malformed CATCHER_EVENT (%zu bytes)", frame.payload.size());
        }
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const bool host = session.linked && session.localSlot == session.hostSlot;
    g_guest = session.linked && !host;
    g_hostSlot = session.hostSlot;
    bt_events::setRole(host, g_guest);
    if (!host) {
        g_sentOnce = false;
        return;
    }
    sendRegions(net);
    for (const bt_wire::CatcherEvent& event : bt_events::takeCatcherEvents()) {
        if (!net.send(bt_wire::kMsgCatcherEvent, true, proto::kSlotAll, proto::bytesOf(event))) {
            logger::write("bt_sync: could not send a catcher event");
        }
    }
}

}  // namespace bt_sync
