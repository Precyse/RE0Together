#include "cutscene_sync.h"

#include <algorithm>
#include <chrono>
#include <set>

#include "cutscene_gate.h"
#include "cutscene_wire.h"
#include "game.h"
#include "log.h"

namespace {

constexpr uint32_t kHalfRttDivisor = 2;  // the host starts its own copy this long after sending GO, as the guest's GO arrives

// Net thread only.
bool g_guest = false;
bool g_host = false;
uint8_t g_hostSlot = 0;
cutscene_gate::ReadyGate g_gate;
uint16_t g_slowestRttMs = 0;

uint64_t nowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void send(NetClient& net, uint16_t type, uint8_t slot, std::span<const uint8_t> payload, const char* what) {
    if (!net.send(type, true, slot, payload)) logger::write("cutscene_sync: could not send %s", what);
}

void hostTick(NetClient& net, const SessionSnapshot& session) {
    std::set<uint8_t> peers;
    g_slowestRttMs = 0;
    for (const PeerInfo& peer : session.peers) {
        peers.insert(peer.slot);
        g_slowestRttMs = std::max(g_slowestRttMs, peer.rttMs);
    }
    for (const cutscene_wire::Start& start : game::takeCutsceneStarts()) {
        send(net, cutscene_wire::kMsgStart, proto::kSlotAll, proto::bytesOf(start), "START");
        g_gate.begin(start.id, peers, nowMs());
    }
    for (const cutscene_gate::Opened& opened : g_gate.takeOpened(nowMs())) {
        const cutscene_wire::Go go{opened.id};
        send(net, cutscene_wire::kMsgGo, proto::kSlotAll, proto::bytesOf(go), "GO");
        game::releaseCutscene(opened.id, g_slowestRttMs / kHalfRttDivisor);
        logger::write("cutscene_sync: cutscene %u released%s", opened.id, opened.timedOut ? " (a guest did not answer in time)" : "");
    }
    for (const cutscene_wire::End& end : game::takeCutsceneEnds()) {
        send(net, cutscene_wire::kMsgEnd, proto::kSlotAll, proto::bytesOf(end), "END");
    }
}

void guestTick(NetClient& net) {
    for (const uint32_t id : game::takeCutsceneReady()) {
        const cutscene_wire::Ready ready{id};
        send(net, cutscene_wire::kMsgReady, g_hostSlot, proto::bytesOf(ready), "READY");
    }
}

void fromHost(const GameFrame& frame) {
    if (!g_guest || frame.slot != g_hostSlot) return;
    if (frame.type == cutscene_wire::kMsgStart) {
        cutscene_wire::Start start;
        if (cutscene_wire::decode(frame.payload, start)) game::armCutscene(start);
        else logger::write("cutscene_sync: dropped a malformed START (%zu bytes)", frame.payload.size());
    } else if (frame.type == cutscene_wire::kMsgGo) {
        cutscene_wire::Go go;
        if (cutscene_wire::decode(frame.payload, go)) game::goCutscene(go.id);
    } else if (frame.type == cutscene_wire::kMsgEnd) {
        cutscene_wire::End end;
        if (cutscene_wire::decode(frame.payload, end)) game::endCutscene(end);
    }
}

}  // namespace

namespace cutscene_sync {

void onFrame(const GameFrame& frame) {
    if (frame.type == cutscene_wire::kMsgReady) {
        cutscene_wire::Ready ready;
        if (g_host && cutscene_wire::decode(frame.payload, ready)) g_gate.ready(ready.id, frame.slot);
        return;
    }
    fromHost(frame);
}

void tick(NetClient& net, const SessionSnapshot& session) {
    g_host = session.linked && session.localSlot == session.hostSlot;
    g_guest = session.linked && !g_host;
    g_hostSlot = session.hostSlot;
    game::setCutsceneRole(g_host, g_guest, g_host ? session.peers.size() : 0);
    if (g_host) {
        hostTick(net, session);
    } else if (g_guest) {
        guestTick(net);
    }
}

}  // namespace cutscene_sync
