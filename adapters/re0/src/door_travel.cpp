#include "door_travel.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>

#include "debug_overlay.h"
#include "debug_stats.h"
#include "floor_items_sync.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "log.h"
#include "net_pad.h"
#include "protocol.h"
#include "state_correction.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kRoomStateInterval = std::chrono::seconds(2);
constexpr auto kDesyncAfter = std::chrono::seconds(3);
constexpr float kDesyncToastSeconds = 4.0f;

NetClient* g_net = nullptr;

std::mutex g_mutex;
door_travel::RoomState g_peer{};  // guarded by g_mutex
bool g_hasPeer = false;           // guarded by g_mutex

std::atomic<bool> g_doorWasActive{false};  // set by whichever thread first sees the door running
Clock::time_point g_lastSend;
bool g_mismatching = false;
Clock::time_point g_mismatchSince;
bool g_desyncReported = false;

bool partnerInRoom() { return game_state::inCurrentRoom(game::partner()); }

void sendRoomState() {
    const door_travel::RoomState state{game_state::currentRoom(), partnerInRoom(), 0};
    g_lastSend = Clock::now();
    g_net->send(proto::kMsgRoomState, true, proto::kSlotAll, {reinterpret_cast<const uint8_t*>(&state), sizeof(state)});
}

// Level-triggered from the game tick and the net thread, so the start is seen even when the game stops ticking.
// Whether the partner comes along is the game's own follow logic (party_mode keeps its follow flag).
void pollDoorStart() {
    if (!game_state::doorActive() || g_doorWasActive.exchange(true)) return;
    logger::write("door_travel: door started, partner %s the focused character",
                  partnerInRoom() ? "with" : "not with");
}

void onArrival() {
    sendRoomState();
    state_correction::requestForcedCheck();
    floor_items_sync::onArrival();
    logger::write("door_travel: arrived in room 0x%04x", game_state::currentRoom());
}

void forgetPeer() {
    std::lock_guard lock(g_mutex);
    g_hasPeer = false;
}

bool peerReport(door_travel::RoomState& out) {
    std::lock_guard lock(g_mutex);
    out = g_peer;
    return g_hasPeer;
}

// Both sides see their two characters together, yet the peer is in another room than us.
void checkDesync(Clock::time_point now) {
    door_travel::RoomState peer;
    const bool mismatch = peerReport(peer) && peer.partnerInRoom && partnerInRoom() && !game_state::doorActive() &&
                          peer.room != game_state::currentRoom();
    if (!mismatch) {
        g_mismatching = false;
        g_desyncReported = false;
        return;
    }
    if (!g_mismatching) {
        g_mismatching = true;
        g_mismatchSince = now;
        return;
    }
    if (g_desyncReported || now - g_mismatchSince < kDesyncAfter) return;
    g_desyncReported = true;
    debug_stats::count(debug_stats::Counter::RoomDesyncs);
    debug_overlay::toast("Room desync", kDesyncToastSeconds);
    logger::write("door_travel: room desync, local 0x%04x peer 0x%04x", game_state::currentRoom(), peer.room);
}

void onTick() {
    debug_stats::set(debug_stats::Gauge::DoorPhase, game_state::doorPhase());
    debug_stats::set(debug_stats::Gauge::Room, game_state::currentRoom());
    if (!net_pad::active()) {
        forgetPeer();
        return;
    }
    pollDoorStart();
    if (!game_state::doorActive() && g_doorWasActive.exchange(false)) onArrival();
    const auto now = Clock::now();
    if (now - g_lastSend >= kRoomStateInterval) sendRoomState();
    checkDesync(now);
}

}  // namespace

namespace door_travel {

void onFrame(const GameFrame& frame) {
    if (frame.type != proto::kMsgRoomState || frame.payload.size() != sizeof(RoomState) ||
        frame.slot != net_pad::peerSlot()) {
        return;
    }
    std::lock_guard lock(g_mutex);
    std::memcpy(&g_peer, frame.payload.data(), sizeof(g_peer));
    g_hasPeer = true;
}

void onNetTick() {
    if (net_pad::active()) pollDoorStart();
}

void enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("door_travel", onTick);
}

}  // namespace door_travel
