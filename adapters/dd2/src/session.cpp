#include "session.h"

#include <cstring>
#include <map>
#include <vector>

#include "body_card.h"
#include "load_slot.h"
#include "log.h"
#include "net_client.h"
#include "peer_bodies.h"
#include "peer_roster.h"
#include "enemy_mirror.h"
#include "enemy_stream.h"
#include "roster.h"
#include "session_rule.h"
#include "world_clock.h"
#include "world_gate.h"

namespace {

constexpr uint8_t kAllPeers = 0xFF;
constexpr uint64_t kPlaceResendMs = 2000;

// Leaked on purpose: joining the net thread from a static destructor would run under the loader lock.
NetClient& g_net = *new NetClient;

struct Peer {
    bool helloOk = false;
    bool placeKnown = false;
    wire_dd2::Place place{};
    uint64_t lastFrameMs = 0;  // when the newest frame of any kind arrived (or the peer was first seen)
    bool staleLogged = false;
};

wire_dd2::GameHello g_hello{};
PeerRoster<Peer> g_peers;
SessionSnapshot g_snapshot;
wire_dd2::Place g_lastPlace{};
uint64_t g_lastPlaceMs = 0;
uint32_t g_refused = 0;
uint64_t g_nowMs = 0;  // the newest tick's time, for the staleness of a peer
std::map<uint8_t, uint32_t> g_joinSerials;  // per slot, the join serial its modules' state belongs to

// A peer that left, or a new one on its slot: the modules keeping per-peer state start over for that slot.
void resetLeftPeers() {
    std::map<uint8_t, uint32_t> now;
    for (const PeerInfo& info : g_snapshot.peers) now[info.slot] = info.joinSerial;
    for (const auto& [slot, serial] : g_joinSerials) {
        const auto it = now.find(slot);
        if (it == now.end() || it->second != serial) peer_bodies::forget(slot);
    }
    g_joinSerials = std::move(now);
}

void onHello(uint8_t slot, Peer& peer, const wire_dd2::GameHello& hello) {
    peer.helloOk = hello.proto == g_hello.proto && hello.peTimestamp == g_hello.peTimestamp &&
                   hello.tdbHash == g_hello.tdbHash && hello.namesHash == g_hello.namesHash;
    logger::write("session: slot %u %s (modules 0x%X)", slot, peer.helloOk ? "plugin matches" : "plugin differs: nothing is shared with it",
                  hello.moduleMask);
}

void onPlace(uint8_t slot, Peer& peer, const wire_dd2::Place& place) {
    if (!peer.placeKnown || place.place != peer.place.place || place.loadSerial != peer.place.loadSerial) {
        logger::write("session: slot %u place %u, load serial %u", slot, place.place, place.loadSerial);
    }
    peer.place = place;
    peer.placeKnown = true;
}

// True when the frame was a session message or went to a module that took it.
bool route(const GameFrame& frame, Peer& peer, uint64_t nowMs) {
    switch (frame.type) {
        case wire_dd2::kGameHello:
            if (const auto hello = wire_dd2::decodeHello(frame.payload)) onHello(frame.slot, peer, *hello);
            return true;
        case wire_dd2::kPlace:
            if (const auto place = wire_dd2::decodePlace(frame.payload)) {
                onPlace(frame.slot, peer, *place);
                return true;
            }
            return false;
        default:
            break;
    }
    if (!peer.helloOk) return false;  // nothing but the session messages from a peer whose plugin differs
    switch (frame.type) {
        case wire_dd2::kBodyState: return peer_bodies::onBodyState(frame.slot, frame.payload, nowMs);
        case wire_dd2::kActionEvent: return peer_bodies::onActionEvent(frame.slot, frame.payload);
        case wire_dd2::kClock: return frame.slot == g_snapshot.hostSlot && world_clock::onClock(frame.payload, nowMs);
        case wire_dd2::kBodyCard: return body_card::onFrame(frame.slot, frame.payload);
        case wire_dd2::kEnemyRoster: return roster::onFrame(frame.slot, frame.payload);
        case wire_dd2::kEnemyState: return frame.slot == g_snapshot.hostSlot && enemy_mirror::onState(frame.payload, nowMs);
        case wire_dd2::kEnemySpawn: return frame.slot == g_snapshot.hostSlot && enemy_mirror::onSpawn(frame.payload);
        case wire_dd2::kEnemyHit: return session::isHost() && enemy_stream::onHit(frame.slot, frame.payload, nowMs);
        case wire_dd2::kEnemyWant: return session::isHost() && enemy_stream::onWant(frame.slot, frame.payload);
        default: return false;
    }
}

// A peer silent for kPeerStaleMs is dropped from the queries below; one line when that starts and when it ends.
void updateStaleness(uint64_t nowMs) {
    for (const PeerInfo& info : g_snapshot.peers) {
        Peer* peer = g_peers.find(info.slot);
        if (!peer) continue;
        if (!peer->lastFrameMs) peer->lastFrameMs = nowMs;
        const bool isStale = session_rule::stale(nowMs, peer->lastFrameMs);
        if (isStale == peer->staleLogged) continue;
        peer->staleLogged = isStale;
        logger::write("session: slot %u %s", info.slot, isStale ? "silent for 6 s, treated as gone" : "is sending again");
    }
}

bool isStale(const Peer& peer) { return session_rule::stale(g_nowMs, peer.lastFrameMs); }

void onFrame(const GameFrame& frame, uint64_t nowMs) {
    Peer* peer = g_peers.find(frame.slot);
    if (!peer) return;
    peer->lastFrameMs = nowMs;
    if (!route(frame, *peer, nowMs) && ++g_refused == 1) {
        logger::write("session: refused a frame 0x%04X from slot %u (first of its kind is logged)", frame.type, frame.slot);
    }
}

wire_dd2::Place ownPlace() {
    wire_dd2::Place place{};
    place.place = static_cast<uint8_t>(world_gate::place());
    place.loadSerial = world_gate::loadSerial();
    place.loadSlot = static_cast<int8_t>(load_slot::current());
    return place;
}

}  // namespace

namespace session {

void start(uint16_t port, const wire_dd2::GameHello& hello) {
    g_hello = hello;
    g_net.start(port, "dd2");
    logger::write("session: connecting to the launcher on port %u", port);
}

void tick(uint64_t nowMs) {
    g_nowMs = nowMs;
    // Frames are held until the roster has seen this poll's joins, so a joining peer's first frames are kept.
    std::vector<GameFrame> frames;
    g_snapshot = g_net.poll([&](const GameFrame& frame) { frames.push_back(frame); });
    const bool newPeer = g_peers.update(g_snapshot);
    resetLeftPeers();
    for (const GameFrame& frame : frames) onFrame(frame, nowMs);
    updateStaleness(nowMs);
    if (!g_snapshot.linked || g_peers.empty()) return;
    if (newPeer) sendMessage(wire_dd2::kGameHello, true, g_hello);
    const wire_dd2::Place place = ownPlace();
    if (newPeer || nowMs - g_lastPlaceMs >= kPlaceResendMs || std::memcmp(&place, &g_lastPlace, sizeof(place)) != 0) {
        sendMessage(wire_dd2::kPlace, true, place);
        g_lastPlace = place;
        g_lastPlaceMs = nowMs;
    }
}

bool send(uint16_t type, bool reliable, std::span<const uint8_t> payload) {
    return g_net.send(type, reliable, kAllPeers, payload);
}

bool active() {
    if (!g_snapshot.linked) return false;
    for (const PeerInfo& info : g_snapshot.peers) {
        if (info.slot == g_snapshot.localSlot) continue;
        if (const Peer* peer = g_peers.find(info.slot); peer && peer->helloOk) return true;
    }
    return false;
}

bool isHost() { return g_snapshot.linked && g_snapshot.localSlot == g_snapshot.hostSlot; }

std::optional<wire_dd2::Place> hostPlace() {
    if (isHost()) return std::nullopt;
    return peerPlace(g_snapshot.hostSlot);
}

std::optional<wire_dd2::Place> peerPlace(uint8_t slot) {
    const Peer* peer = g_peers.find(slot);
    if (!peer || !peer->placeKnown || isStale(*peer)) return std::nullopt;
    return peer->place;
}

std::vector<uint8_t> matchingPeers() {
    std::vector<uint8_t> out;
    for (const PeerInfo& info : g_snapshot.peers) {
        if (info.slot == g_snapshot.localSlot) continue;
        if (const Peer* peer = g_peers.find(info.slot); peer && peer->helloOk && !isStale(*peer)) out.push_back(info.slot);
    }
    return out;
}

bool stale(uint8_t slot) {
    const Peer* peer = g_peers.find(slot);
    return !peer || isStale(*peer);
}

std::optional<uint8_t> hostSlot() {
    if (!g_snapshot.linked) return std::nullopt;
    return g_snapshot.hostSlot;
}

std::vector<PeerView> peers() {
    std::vector<PeerView> out;
    for (const PeerInfo& info : g_snapshot.peers) {
        if (info.slot == g_snapshot.localSlot) continue;
        const Peer* peer = g_peers.find(info.slot);
        if (!peer) continue;
        out.push_back({info.slot, info.steamId, info.name, peer->helloOk, isStale(*peer),
                       peer->placeKnown ? std::optional(peer->place) : std::nullopt});
    }
    return out;
}

}  // namespace session
