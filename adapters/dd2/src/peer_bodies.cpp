#include "peer_bodies.h"

#include <cmath>
#include <map>
#include <utility>

#include "log.h"
#include "owner_stream.h"
#include "stream_clock.h"

namespace {

constexpr uint64_t kSummaryPeriodMs = 30000;
constexpr uint64_t kUsPerMs = 1000;
struct Peer {
    stream_clock::StreamClock clock;
    std::optional<wire_dd2::BodyState> bodies[wire_dd2::kBodyKinds];
    uint32_t states = 0, actions = 0, stale = 0;
    uint32_t lastAction = 0;  // the Arisen's
    std::vector<wire_dd2::ActionEvent> edges[wire_dd2::kBodyKinds];
};

std::map<uint8_t, Peer> g_peers;
uint64_t g_lastSummaryMs = 0;

double distanceTo(const wire_dd2::BodyState& body) {
    const auto local = owner_stream::lastSample();
    if (!local) return -1.0;
    double sum = 0.0;
    for (int axis = 0; axis < 3; ++axis) {
        const double d = body.position[axis] - local->position[axis];
        sum += d * d;
    }
    return std::sqrt(sum);
}

}  // namespace

namespace peer_bodies {

bool onBodyState(uint8_t slot, std::span<const uint8_t> payload, uint64_t nowMs) {
    const auto state = wire_dd2::decodeBodyState(payload);
    if (!state) return false;
    Peer& peer = g_peers[slot];
    peer.clock.observe(static_cast<uint32_t>(state->senderUs / kUsPerMs), static_cast<uint32_t>(nowMs));
    std::optional<wire_dd2::BodyState>& body = peer.bodies[state->kind];
    if (body && !stream_clock::after(state->seq, body->seq)) {
        ++peer.stale;  // unreliable frames may arrive out of order: the newest wins
        return true;
    }
    body = *state;
    ++peer.states;
    return true;
}

bool onActionEvent(uint8_t slot, std::span<const uint8_t> payload) {
    const auto event = wire_dd2::decodeActionEvent(payload);
    if (!event) return false;
    Peer& peer = g_peers[slot];
    ++peer.actions;
    if (event->layer == 0 && event->kind == wire_dd2::kKindArisen) peer.lastAction = event->actionHash;
    std::vector<wire_dd2::ActionEvent>& edges = peer.edges[event->kind];
    if (edges.size() == peer_bodies::kMaxPendingEdges) edges.erase(edges.begin());
    edges.push_back(*event);
    return true;
}

void forget(uint8_t slot) { g_peers.erase(slot); }

std::vector<wire_dd2::ActionEvent> takeActionEdges(uint8_t slot, uint8_t kind) {
    const auto it = g_peers.find(slot);
    if (it == g_peers.end() || kind >= wire_dd2::kBodyKinds) return {};
    return std::exchange(it->second.edges[kind], {});
}

std::optional<wire_dd2::BodyState> latest(uint8_t slot, uint8_t kind) {
    const auto it = g_peers.find(slot);
    return it == g_peers.end() || kind >= wire_dd2::kBodyKinds ? std::nullopt : it->second.bodies[kind];
}

void tick(uint64_t nowMs) {
    if (nowMs - g_lastSummaryMs < kSummaryPeriodMs) return;
    g_lastSummaryMs = nowMs;
    for (auto& [slot, peer] : g_peers) {
        if (!peer.states && !peer.actions) continue;
        const auto& arisen = peer.bodies[wire_dd2::kKindArisen];
        const double distance = arisen ? distanceTo(*arisen) : -1.0;
        logger::write("peer_bodies: slot %u: %u states, %u actions, %u stale in 30 s, %.1f m away, load serial %u, action 0x%08X",
                      slot, peer.states, peer.actions, peer.stale, distance, arisen ? arisen->loadSerial : 0,
                      peer.lastAction);
        peer.states = peer.actions = peer.stale = 0;
    }
}

}  // namespace peer_bodies
