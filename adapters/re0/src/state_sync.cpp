#include "state_sync.h"

#include <chrono>
#include <cstring>
#include <map>
#include <string>

#include "camera_parity.h"
#include "command_input.h"
#include "character_owner.h"
#include "debug_overlay.h"
#include "debug_stats.h"
#include "enemy_net.h"
#include "enemy_protocol.h"
#include "enemy_state.h"
#include "door_sync.h"
#include "flag_sync.h"
#include "join_sync.h"
#include "auto_join.h"
#include "phase_watch.h"
#include "session_slot.h"
#include "door_travel.h"
#include "game.h"
#include "game_state.h"
#include "log.h"
#include "floor_items_sync.h"
#include "inventory_sync.h"
#include "menu_mirror.h"
#include "net_pad.h"
#include "pad_frame.h"
#include "party_mode.h"
#include "state_correction.h"
#include "player_damage.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr const char* kGameId = "re0";
constexpr auto kSendInterval = std::chrono::microseconds(static_cast<int>(1'000'000 / state_sync::kSendHz));
constexpr auto kLogInterval = std::chrono::seconds(1);
constexpr float kPeerToastSeconds = 4.0f;

Clock::time_point g_lastSend;
Clock::time_point g_lastLog;
uint32_t g_seq = 0;
std::map<uint8_t, std::string> g_knownPeers;  // slot -> name, as of the last tick

// Toasts peers that appeared or disappeared since the previous tick.
void announcePeerChanges(const SessionSnapshot& session) {
    std::map<uint8_t, std::string> now;
    for (const PeerInfo& peer : session.peers) now[peer.slot] = peer.name;
    for (const auto& [slot, name] : now) {
        if (!g_knownPeers.contains(slot)) debug_overlay::toast(("Co-op player joined: " + name).c_str(), kPeerToastSeconds);
    }
    for (const auto& [slot, name] : g_knownPeers) {
        if (!now.contains(slot)) debug_overlay::toast("Co-op player left", kPeerToastSeconds);
    }
    g_knownPeers = std::move(now);
}

void sendLocalState(NetClient& net) {
    state_sync::PlayerState state{};
    const character_owner::Character owned = character_owner::localCharacter();
    const uintptr_t player = character_owner::find(owned);
    if (owned == character_owner::Character::Unknown || !join_sync::caughtUp() ||
        !game::readTransform(player, state.pos, state.quat)) {
        return;
    }
    state.characterId = static_cast<uint8_t>(owned);
    state.focusedCharacterId = static_cast<uint8_t>(character_owner::identify(game::controlled()));
    if (!game::readMemory(player + game::kPlayerHpOffset, state.hp)) return;
    state.senderIsHost = character_owner::isHost();
    state.room = game_state::currentRoom();
    state.seq = ++g_seq;
    if (net.send(state_sync::kMsgPlayerState, false, proto::kSlotAll,
                 {reinterpret_cast<const uint8_t*>(&state), sizeof(state)})) {
        debug_stats::count(debug_stats::Counter::PlayerStateSent);
    }
}

void logRemoteState(const GameFrame& frame) {
    if (frame.type != state_sync::kMsgPlayerState || frame.payload.size() != sizeof(state_sync::PlayerState)) return;
    const auto now = Clock::now();
    if (now - g_lastLog < kLogInterval) return;
    g_lastLog = now;
    state_sync::PlayerState state;
    std::memcpy(&state, frame.payload.data(), sizeof(state));
    logger::write("PLAYER_STATE from slot=%u seq=%u pos=(%.2f, %.2f, %.2f)", frame.slot, state.seq,
                  state.pos[0], state.pos[1], state.pos[2]);
}

void onFrame(const GameFrame& frame) {
    if (frame.type == proto::kMsgPartyRequest || frame.type == proto::kMsgPartyMode) {
        party_mode::onFrame(frame);
        return;
    }
    if (frame.type == pad::kMsgPadFrame) {
        net_pad::onPacket(frame);
        return;
    }
    if (frame.type == character_owner::kMsgOwnership) {
        character_owner::onFrame(frame);
        return;
    }
    if (frame.type == enemy_protocol::kMsgHitRequest || frame.type == enemy_protocol::kMsgHitApplied) {
        enemy_net::onFrame(frame);
        return;
    }
    if (frame.type == player_damage::kMsgPlayerDied) {
        player_damage::onFrame(frame);
        return;
    }
    if (frame.type == enemy_protocol::kMsgEnemyState) {
        enemy_state::onFrame(frame);
        return;
    }
    if (frame.type == proto::kMsgSaveSlot) {
        session_slot::onFrame(frame);
        return;
    }
    if (frame.type == proto::kMsgSnapshotRequest || frame.type == proto::kMsgJoinSnapshot) {
        join_sync::onFrame(frame);
        return;
    }
    if (frame.type == proto::kMsgFlagDiff) {
        flag_sync::onFrame(frame);
        return;
    }
    if (frame.type == proto::kMsgDoorChange) {
        door_sync::onFrame(frame);
        return;
    }
    if (frame.type == proto::kMsgRoomState) {
        door_travel::onFrame(frame);
        return;
    }
    if (frame.type == proto::kMsgMenuState) {
        menu_mirror::onFrame(frame);
        return;
    }
    if (frame.type == proto::kMsgInventory) {
        inventory_sync::onFrame(frame);
        return;
    }
    if (frame.type == proto::kMsgFloorPut || frame.type == proto::kMsgFloorTake) {
        floor_items_sync::onFrame(frame);
        return;
    }
    if (frame.type == state_sync::kMsgPlayerState) debug_stats::count(debug_stats::Counter::PlayerStateReceived);
    logRemoteState(frame);
    camera_parity::onFrame(frame);
    state_correction::onFrame(frame);
}

void tick(NetClient& net) {
    const SessionSnapshot session = net.poll(onFrame);
    debug_stats::setSession(session);
    announcePeerChanges(session);
    net_pad::onSession(session);
    character_owner::onSession(session);
    menu_mirror::onNetTick(net);
    command_input::onNetTick();
    door_travel::onNetTick();
    phase_watch::onNetTick();
    session_slot::onNetTick(net);
    auto_join::onNetTick();
    const auto now = Clock::now();
    if (now - g_lastSend < kSendInterval) return;
    g_lastSend = now;
    sendLocalState(net);
}

}  // namespace

namespace state_sync {

void start(NetClient& net, uint16_t port) {
    net.start(port, kGameId, [&net] { tick(net); });
}

}  // namespace state_sync
