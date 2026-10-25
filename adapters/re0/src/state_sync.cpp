#include "state_sync.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <string>

#include "appearance.h"
#include "appearance_rule.h"
#include "auto_join.h"
#include "character_owner.h"
#include "command_input.h"
#include "debug_overlay.h"
#include "host_exit.h"
#include "debug_stats.h"
#include "dev_command.h"
#include "door_sync.h"
#include "door_travel.h"
#include "enemy_net.h"
#include "enemy_protocol.h"
#include "enemy_state.h"
#include "event_place.h"
#include "event_sync.h"
#include "flag_sync.h"
#include "floor_items_sync.h"
#include "frame_router.h"
#include "game.h"
#include "game_state.h"
#include "inventory_sync.h"
#include "join_sync.h"
#include "log.h"
#include "menu_mirror.h"
#include "net_pad.h"
#include "net_trace.h"
#include "pad_frame.h"
#include "partner_hud.h"
#include "party_mode.h"
#include "phase_watch.h"
#include "player_damage.h"
#include "pose_capture.h"
#include "resync.h"
#include "room_authority.h"
#include "lobby.h"
#include "session_slot.h"
#include "scene.h"
#include "state_correction.h"
#include "state_mode.h"
#include "state_net_rule.h"
#include "world_digest.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr const char* kGameId = "re0";
constexpr auto kSendInterval = std::chrono::microseconds(static_cast<int>(1'000'000 / state_sync::kSendHz));
constexpr auto kLogInterval = std::chrono::seconds(1);
constexpr float kPeerToastSeconds = 4.0f;

std::atomic<bool> g_modeMismatchNoted{false};
Clock::time_point g_lastSend;
Clock::time_point g_lastLog;
uint32_t g_seq = 0;

struct KnownPeer {
    std::string name;
    uint32_t joinSerial;  // the PEER_UP this slot's peer was last announced by
};
std::map<uint8_t, KnownPeer> g_knownPeers;  // slot -> peer, as of the last tick
SessionSnapshot g_lastSession;                // the previous tick's session without its peers

// This guest's host went away: it goes back to the main menu, since nothing played on would reach the host's save.
void announceHostLeft(const std::string& name) {
    host_exit::request();
    logger::write("state_sync: host %s left, returning to the main menu", name.c_str());
}

// Toasts peers that appeared or disappeared since the previous tick. A PEER_UP for a slot that is already up (a new join
// serial) means the peer was unreachable for a while and frames may be lost: a guest asks for a new snapshot, the host
// waits for the guest's request.
void announcePeerChanges(const SessionSnapshot& session) {
    const bool guest = session.linked && session.localSlot != session.hostSlot;
    std::map<uint8_t, KnownPeer> now;
    for (const PeerInfo& peer : session.peers) now[peer.slot] = {peer.name, peer.joinSerial};
    for (const auto& [slot, peer] : now) {
        const auto known = g_knownPeers.find(slot);
        if (known == g_knownPeers.end()) {
            debug_overlay::toast((peer.name + " joined the game.").c_str(), kPeerToastSeconds, toast_queue::Style::Line);
            partner_hud::onPeerJoined();
        } else if (guest && known->second.joinSerial != peer.joinSerial) {
            resync::request("partner reconnected");
        }
    }
    for (const auto& [slot, peer] : g_knownPeers) {
        if (now.contains(slot)) continue;
        debug_overlay::toast((peer.name + " left the game.").c_str(), kPeerToastSeconds, toast_queue::Style::Line);
        const bool wasGuest = g_lastSession.localSlot != g_lastSession.hostSlot;
        if (g_lastSession.linked && wasGuest && slot == g_lastSession.hostSlot) announceHostLeft(peer.name);
    }
    g_knownPeers = std::move(now);
    g_lastSession = session;
    g_lastSession.peers.clear();
}

void sendLocalState(NetClient& net) {
    state_sync::PlayerState state{};
    const character_owner::Character owned = character_owner::localCharacter();
    const uintptr_t player = character_owner::find(owned);
    if (owned == character_owner::Character::Unknown || !join_sync::caughtUp() ||
        !game::readWorldTransform(player, state.pos, state.quat)) {
        return;
    }
    state.characterId = static_cast<uint8_t>(owned);
    state.focusedCharacterId = static_cast<uint8_t>(character_owner::identify(game::controlled()));
    if (!game::readMemory(player + game::kPlayerHpOffset, state.hp)) return;
    state.senderIsHost = character_owner::isHost();
    state.room = scene::current();
    state.seq = ++g_seq;
    if (net.send(state_sync::kMsgPlayerState, false, proto::kSlotAll, proto::bytesOf(state))) {
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

// Nothing negotiates partner_mode between the machines: a peer on the other mode shows in the types it sends. Said once.
void noteModeMismatch(const GameFrame& frame) {
    if (!state_net_rule::peerRunsOtherMode(state_mode::on(), frame.type) || g_modeMismatchNoted.exchange(true)) return;
    const char* const own = partnerModeName(state_mode::on() ? PartnerMode::State : PartnerMode::Input);
    const char* const peer = partnerModeName(state_mode::on() ? PartnerMode::Input : PartnerMode::State);
    logger::write("state_sync: the peer runs partner_mode=%s (type 0x%04x from slot %u), this machine runs %s", peer, frame.type,
                  frame.slot, own);
    debug_stats::setError("Partner runs partner_mode=%s, this machine %s", peer, own);
}

void onFrame(const GameFrame& frame) {
    net_trace::record(frame);
    noteModeMismatch(frame);
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
    if (frame.type == enemy_protocol::kMsgHitRequest || frame.type == enemy_protocol::kMsgHitApplied ||
        frame.type == enemy_protocol::kMsgEnemyDecision) {
        enemy_net::onFrame(frame);
        return;
    }
    if (frame.type == appearance_rule::kMsgAppearance) {
        appearance::onFrame(frame);
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
    if (frame.type == lobby_wire::kMsgLobbyPresence || frame.type == lobby_wire::kMsgSessionLoad) {
        lobby::onFrame(frame);
        return;
    }
    if (frame.type == proto::kMsgSnapshotRequest || frame.type == proto::kMsgJoinSnapshot) {
        join_sync::onFrame(frame);
        return;
    }
    if (frame.type == proto::kMsgCharacterPlace) {
        event_place::onFrame(frame);
        return;
    }
    if (frame.type == proto::kMsgEventStart || frame.type == proto::kMsgEventSteps) {
        event_sync::onFrame(frame);
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
    if (frame.type == proto::kMsgResyncRequest) {
        resync::onFrame(frame);
        return;
    }
    if (frame.type == proto::kMsgFloorPut || frame.type == proto::kMsgFloorTake || frame.type == proto::kMsgFloorSnapshot) {
        floor_items_sync::onFrame(frame);
        return;
    }
    if (state_mode::on()) {
        if (frame_router::dispatch(frame, g_lastSession.hostSlot)) return;
    } else if (state_net_rule::isStateType(frame.type)) {
        return;  // a state-mode peer's frame: nothing here handles it (noted once above)
    }
    if (frame.type == state_sync::kMsgPlayerState) debug_stats::count(debug_stats::Counter::PlayerStateReceived);
    logRemoteState(frame);
    state_correction::onFrame(frame);
    partner_hud::onFrame(frame);
}

void tick(NetClient& net) {
    const SessionSnapshot session = net.poll(onFrame);
    debug_stats::setSession(session);
    announcePeerChanges(session);
    net_pad::onSession(session);
    if (state_mode::on()) room_authority::onSession(session);
    character_owner::onSession(session);
    partner_hud::onNetTick(session);
    menu_mirror::onNetTick(net);
    command_input::onNetTick();
    door_travel::onNetTick();
    phase_watch::onNetTick();
    session_slot::onNetTick(net);
    lobby::onNetTick(net, session);
    auto_join::onNetTick();
    resync::onNetTick();
    dev_command::onNetTick();
    if (state_mode::on()) {
        pose_capture::onNetTick();
        world_digest::onNetTick();
    }
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
