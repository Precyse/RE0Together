#include "link.h"

#include <windows.h>

#include <cstring>
#include <vector>

#include "build_gate.h"
#include "capture.h"
#include "funnels.h"
#include "log.h"
#include "net_client.h"
#include "peer_roster.h"
#include "containment.h"
#include "equipment.h"
#include "minimap.h"
#include "save_guard.h"
#include "story.h"
#include "world_time.h"
#include "presence_recv.h"
#include "presence_send.h"
#include "self_hash.h"
#include "world_gate.h"

namespace {

constexpr uint8_t kAllPeers = 0xFF;
constexpr uint64_t kWorldResendMs = 2000;
constexpr uint64_t kWorldStaleMs = 6000;  // three missed CD_WORLD resends: the peer's report is no longer its state
constexpr uint64_t kCountersPeriodMs = 5000;

// Leaked on purpose: joining the net thread from a static destructor would run under the loader lock.
NetClient& g_net = *new NetClient;

struct Peer {
    bool helloSeen = false;
    bool helloOk = false;
    bool mismatchLogged = false;
    wire_cd::World world{};
    bool worldKnown = false;
    uint64_t worldAtMs = 0;
};

wire_cd::Hello g_hello{};
PeerRoster<Peer> g_peers;  // main tick only
wire_cd::World g_lastWorld{};
uint64_t g_lastWorldMs = 0;
uint64_t g_lastCountersMs = 0;
std::vector<wire_cd::Counter> g_lastCounters;
uint32_t g_wireRefused = 0;
uint32_t g_ticks = 0;
bool g_joinedThisTick = false;
std::vector<uint8_t> g_slots;
std::vector<game_state_rule::Peer> g_peerLinks;
uint64_t g_nowMs = 0;
bool g_linked = false;
uint8_t g_localSlot = 0, g_hostSlot = 0;

template <typename T>
void sendAll(uint16_t type, const T& message) {
    const std::vector<uint8_t> payload = wire_cd::encode(message);
    g_net.send(type, true, kAllPeers, payload);
}

void onHello(uint8_t slot, Peer& peer, const wire_cd::Hello& hello) {
    const bool same = hello.proto == g_hello.proto && hello.exeTimeDateStamp == g_hello.exeTimeDateStamp &&
                      std::memcmp(hello.adapterHash, g_hello.adapterHash, wire_cd::kHashBytes) == 0;
    peer.helloSeen = true;
    peer.helloOk = same;
    if (same) {
        logger::write("link: slot %u adapter matches (sets 0x%X, catalogue %s, %s)", slot, hello.siteSetMask,
                      hello.catalogueOk ? "ok" : "fail", hello.role == 0 ? "host" : "guest");
    } else if (!peer.mismatchLogged) {
        peer.mismatchLogged = true;
        logger::write("link: slot %u adapter differs (proto %u, exe 0x%08X): nothing is shared with it", slot,
                      hello.proto, hello.exeTimeDateStamp);
    }
}

void onWorld(uint8_t slot, Peer& peer, const wire_cd::World& world) {
    if (!peer.worldKnown || world.gateState != peer.world.gateState || world.loadSerial != peer.world.loadSerial) {
        logger::write("link: slot %u world gate %u, load serial %u", slot, world.gateState, world.loadSerial);
    }
    peer.world = world;
    peer.worldKnown = true;
    peer.worldAtMs = g_nowMs;
}

void onFrame(const GameFrame& frame) {
    Peer* found = g_peers.find(frame.slot);
    if (!found) return;
    Peer& peer = *found;
    switch (frame.type) {
        case wire_cd::kHello:
            if (const auto hello = wire_cd::decodeHello(frame.payload)) {
                onHello(frame.slot, peer, *hello);
                return;
            }
            break;
        case wire_cd::kWorld:
            if (const auto world = wire_cd::decodeWorld(frame.payload)) {
                onWorld(frame.slot, peer, *world);
                return;
            }
            break;
        case wire_cd::kMarker:
            if (const auto marker = wire_cd::decodeMarker(frame.payload)) {
                logger::write("link: slot %u marker %u at its clock %llu", frame.slot, marker->id,
                              static_cast<unsigned long long>(marker->localClockMs));
                return;
            }
            break;
        case wire_cd::kCounters:
            if (wire_cd::decodeCounters(frame.payload)) return;
            break;
        default:
            if (peer.helloOk && (presence_recv::onFrame(frame.slot, frame.type, frame.payload, g_nowMs) ||
                                 equipment::onFrame(frame.slot, frame.type, frame.payload) ||
                                 world_time::onFrame(frame.slot, frame.type, frame.payload, g_nowMs) ||
                                 story::onFrame(frame.slot, frame.type, frame.payload))) {
                return;
            }
            break;
    }
    ++g_wireRefused;
}

std::vector<wire_cd::Counter> counters() {
    const funnels::Totals t = funnels::totals();
    auto low = [](uint64_t v) { return static_cast<uint32_t>(v); };
    const presence_send::Stats sent = presence_send::stats();
    const presence_recv::Stats applied = presence_recv::stats();
    const equipment::Stats equip = equipment::stats();
    const minimap::Stats map = minimap::stats();
    const world_time::Stats time = world_time::stats();
    const story::Stats tale = story::stats();
    const save_guard::Stats saves = save_guard::stats();
    return {{wire_cd::kC2SPackets, low(t.packets[0])},        {wire_cd::kS2CPackets, low(t.packets[1])},
            {wire_cd::kMalformed, low(t.malformed)},          {wire_cd::kEventsDropped, low(t.eventsDropped)},
            {wire_cd::kObserverFaults, low(t.faults)},        {wire_cd::kCaptureDropped, low(capture::dropped())},
            {wire_cd::kWireRefused, g_wireRefused},           {wire_cd::kTicks, g_ticks},
            {wire_cd::kTemplatesSent, sent.templatesSent},    {wire_cd::kTemplatesApplied, applied.templatesApplied},
            {wire_cd::kMovesSent, sent.movesSent},            {wire_cd::kMovesApplied, applied.movesApplied},
            {wire_cd::kActionsSent, sent.actionsSent},        {wire_cd::kActionsApplied, applied.actionsApplied},
            {wire_cd::kActionsHeld, applied.actionsHeld},     {wire_cd::kInjected, low(t.injected)},
            {wire_cd::kInjectDropped, low(t.injectDropped)},  {wire_cd::kEquipSent, equip.sent},
            {wire_cd::kEquipApplied, equip.applied},        {wire_cd::kMinimapIcons, map.iconsGiven},
            {wire_cd::kPinsCreated, map.pinsCreated},      {wire_cd::kContainedUpdates, low(containment::skipped())},
            {wire_cd::kFieldTimesPosted, time.posted},       {wire_cd::kFieldTimesConfirmed, time.confirmed},
            {wire_cd::kStoryLogged, tale.logged},            {wire_cd::kStoryReceived, tale.received},
            {wire_cd::kStoryWouldApply, tale.wouldApply},    {wire_cd::kStoryPosted, tale.posted},
            {wire_cd::kStoryApplied, tale.applied},          {wire_cd::kSaveHolds, saves.holds},
            {wire_cd::kSavesHeld, saves.heldSaves}};
}

bool sameCounters(const std::vector<wire_cd::Counter>& a, const std::vector<wire_cd::Counter>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(wire_cd::Counter)) == 0;
}

game_state_rule::Link linkOf(const Peer& peer, uint64_t nowMs) {
    const bool worldStale = peer.worldKnown && nowMs - peer.worldAtMs > kWorldStaleMs;
    return game_state_rule::linkOf(peer.helloSeen, peer.helloOk, worldStale);
}

void sendState(uint64_t nowMs, bool force) {
    const wire_cd::World world = world_gate::report();
    if (force || nowMs - g_lastWorldMs >= kWorldResendMs || std::memcmp(&world, &g_lastWorld, sizeof(world)) != 0) {
        sendAll(wire_cd::kWorld, world);
        g_lastWorld = world;
        g_lastWorldMs = nowMs;
    }
    if (nowMs - g_lastCountersMs < kCountersPeriodMs) return;
    g_lastCountersMs = nowMs;
    const std::vector<wire_cd::Counter> now = counters();
    if (sameCounters(now, g_lastCounters)) return;
    g_lastCounters = now;
    g_net.send(wire_cd::kCounters, false, kAllPeers, wire_cd::encodeCounters(now));
}

}  // namespace

namespace link {

void start(uint16_t port, uint32_t siteSetMask, bool catalogueOk) {
    g_hello.proto = wire_cd::kProto;
    g_hello.exeTimeDateStamp = build_gate::kSupported.timeDateStamp;
    std::memcpy(g_hello.adapterHash, self_hash::adapterHash().data(), wire_cd::kHashBytes);
    g_hello.siteSetMask = siteSetMask;
    g_hello.catalogueOk = catalogueOk ? 1 : 0;
    g_net.start(port, "cd");
    logger::write("link: connecting to the launcher on port %u", port);
}

void tick(uint64_t nowMs) {
    ++g_ticks;
    g_nowMs = nowMs;
    g_joinedThisTick = false;
    // Frames are held until the roster has seen this poll's joins, so a joining peer's first frames are kept.
    std::vector<GameFrame> frames;
    const SessionSnapshot session = g_net.poll([&](const GameFrame& frame) { frames.push_back(frame); });
    const bool newPeer = g_peers.update(session);
    g_linked = session.linked;
    g_localSlot = session.localSlot;
    g_hostSlot = session.hostSlot;
    g_slots.clear();
    g_peerLinks.clear();
    for (const PeerInfo& info : session.peers) {
        if (info.slot == session.localSlot) continue;
        g_slots.push_back(info.slot);
        if (const Peer* peer = g_peers.find(info.slot)) g_peerLinks.push_back({info.steamId, linkOf(*peer, nowMs)});
    }
    for (const GameFrame& frame : frames) onFrame(frame);
    if (!session.linked || g_peers.empty()) return;
    g_hello.role = session.localSlot == session.hostSlot ? 0 : 1;
    if (newPeer) sendAll(wire_cd::kHello, g_hello);
    g_joinedThisTick = newPeer;
    sendState(nowMs, newPeer);
}

bool send(uint16_t type, bool reliable, std::span<const uint8_t> payload) {
    return g_net.send(type, reliable, kAllPeers, payload);
}

bool isLinked() { return g_linked; }

const std::vector<game_state_rule::Peer>& peerLinks() { return g_peerLinks; }

bool peerJoinedThisTick() { return g_joinedThisTick; }

bool isHost() { return g_linked && g_localSlot == g_hostSlot; }

std::optional<uint8_t> hostSlot() {
    if (!g_linked || isHost()) return std::nullopt;
    return g_hostSlot;
}

std::vector<uint8_t> peerSlots() { return g_slots; }

std::optional<wire_cd::World> peerWorld(uint8_t slot) {
    const Peer* peer = g_peers.find(slot);
    if (!peer || !peer->worldKnown || g_nowMs - peer->worldAtMs > kWorldStaleMs) return std::nullopt;
    return peer->world;
}

void sendMarker(uint64_t nowMs) {
    static uint32_t s_id = 0;
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    const wire_cd::Marker marker{++s_id, nowMs, static_cast<uint64_t>(qpc.QuadPart)};
    sendAll(wire_cd::kMarker, marker);
    logger::write("link: marker %u sent at %llu", marker.id, static_cast<unsigned long long>(nowMs));
}

}  // namespace link
