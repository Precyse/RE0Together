#include "door_travel.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>

#include "character_owner.h"
#include "debug_overlay.h"
#include "debug_stats.h"
#include "floor_items_sync.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "log.h"
#include "net_pad.h"
#include "protocol.h"
#include "scene.h"
#include "split_rooms.h"
#include "state_correction.h"

namespace {

using Clock = std::chrono::steady_clock;
using door_travel::PeerPlace;

constexpr auto kRoomStateInterval = std::chrono::seconds(2);
constexpr auto kDesyncAfter = std::chrono::seconds(3);
constexpr float kDesyncToastSeconds = 4.0f;

NetClient* g_net = nullptr;

std::mutex g_mutex;
door_travel::RoomState g_peer{};  // guarded by g_mutex
bool g_hasPeer = false;           // guarded by g_mutex

std::atomic<bool> g_doorWasActive{false};  // set by whichever thread first sees the door running
std::atomic<bool> g_enemyClaim{false};     // this machine was in its loaded room before the peer
Clock::time_point g_lastSend;
bool g_mismatching = false;
Clock::time_point g_mismatchSince;
bool g_desyncReported = false;
bool g_ranEnemies = false;  // game thread: the last logged enemy authority

bool partnerInRoom() { return game_state::inCurrentRoom(game::partner()); }

void sendRoomState() {
    const door_travel::RoomState state{scene::current(), partnerInRoom(), g_enemyClaim};
    g_lastSend = Clock::now();
    g_net->send(proto::kMsgRoomState, true, proto::kSlotAll, proto::bytesOf(state));
}

// Level-triggered from the game tick and the net thread, so the start is seen even when the game stops ticking.
// Whether the partner comes along is the game's own follow logic (party_mode keeps its follow flag).
void pollDoorStart() {
    if (!game_state::doorActive() || g_doorWasActive.exchange(true)) return;
    uint8_t follow = 0;
    const uintptr_t sPlayer = game::readPointer(game::kPlayerGlobal);
    const bool follows = sPlayer && game::readMemory(sPlayer + game::kPlayerFollowOffset, follow) && follow;
    logger::write("door_travel: door started, partner %s", partnerInRoom() && follows ? "follows" : "stays");
}

void onArrival() {
    g_enemyClaim = door_travel::peerPlace() != PeerPlace::Here;
    sendRoomState();
    split_rooms::onArrival();
    state_correction::requestForcedCheck();
    floor_items_sync::onArrival();
    logger::write("door_travel: arrived in scene 0x%02x%s", scene::current(), g_enemyClaim ? ", first here" : "");
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
                          door_travel::peerPlace() == PeerPlace::Elsewhere;
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
    logger::write("door_travel: room desync, local scene 0x%02x peer 0x%02x", scene::current(), peer.scene);
}

void logEnemyAuthority() {
    const bool runs = door_travel::enemyAuthority();
    if (runs == g_ranEnemies) return;
    g_ranEnemies = runs;
    logger::write("door_travel: enemies of scene 0x%02x run %s", scene::current(), runs ? "here" : "on the peer");
}

void onTick() {
    debug_stats::set(debug_stats::Gauge::DoorPhase, game_state::doorPhase());
    debug_stats::set(debug_stats::Gauge::Room, scene::current());
    if (!net_pad::active()) {
        forgetPeer();
        g_enemyClaim = false;
        return;
    }
    pollDoorStart();
    // The door phase goes idle before the new room is in place; arrival waits for the room.
    if (!game_state::doorActive() && game_state::currentRoom() != game_state::kRoomLoading &&
        g_doorWasActive.exchange(false)) {
        onArrival();
    }
    const auto now = Clock::now();
    if (now - g_lastSend >= kRoomStateInterval) sendRoomState();
    checkDesync(now);
    logEnemyAuthority();
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

PeerPlace peerPlace() {
    RoomState peer;
    const uint16_t here = scene::current();
    if (!peerReport(peer) || peer.scene == scene::kNone || here == scene::kNone) return PeerPlace::Unknown;
    return peer.scene == here ? PeerPlace::Here : PeerPlace::Elsewhere;
}

bool enemyAuthority() {
    RoomState peer{};
    peerReport(peer);
    return control_rule::runsEnemies(peerPlace(), character_owner::isHost(), g_enemyClaim, peer.enemyClaim != 0);
}

void onNetTick() {
    if (net_pad::active()) pollDoorStart();
}

void enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("door_travel", onTick);
}

}  // namespace door_travel
