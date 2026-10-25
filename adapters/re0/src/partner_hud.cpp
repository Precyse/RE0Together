#include "partner_hud.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

#include "debug_stats.h"
#include "door_travel.h"
#include "game.h"
#include "lobby.h"
#include "menu_mirror.h"
#include "partner_status.h"
#include "state_sync.h"

namespace {

int g_maxHp = partner_status::kBaseMaxHp;         // the highest hp the partner reported this session
partner_status::Tag g_shown;                     // the tag the overlay has now
state_sync::PlayerState g_partnerState{};          // net thread only: the partner's latest PLAYER_STATE
int g_partnerStateSlot = -1;                      // the slot it came from, -1 before the first
std::chrono::steady_clock::time_point g_partnerStateAt;  // when it arrived

partner_status::Input gather(const PeerInfo& peer) {
    partner_status::Input input;
    input.name = peer.name;
    input.inMenu = menu_mirror::peerMenuOpen();
    input.sameRoom = door_travel::peerPlace() == door_travel::PeerPlace::Here;
    if (g_partnerStateSlot == peer.slot) {
        g_maxHp = std::max(g_maxHp, static_cast<int>(g_partnerState.hp));
        input.hasState = true;
        input.hp = g_partnerState.hp;
        input.maxHp = g_maxHp;
        input.room = g_partnerState.room;
    }
    return input;
}

}  // namespace

namespace partner_hud {

void onNetTick(const SessionSnapshot& session) {
    const PeerInfo* peer = otherPeer(session.peers, session.localSlot);
    partner_status::Tag tag;
    if (peer) {
        const partner_status::Input input = gather(*peer);
        const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_partnerStateAt).count();
        const bool shown = partner_status::shows(game::controlled() != 0, lobby::inGame(peer->slot), input.hasState,
                                                 static_cast<uint32_t>(std::min<long long>(ageMs, UINT32_MAX)));
        if (shown) tag = partner_status::tag(input);
    }
    if (tag == g_shown) return;
    g_shown = tag;
    debug_stats::setPartnerTag(tag);
}

void onFrame(const GameFrame& frame) {
    if (frame.type != state_sync::kMsgPlayerState || frame.payload.size() != sizeof(state_sync::PlayerState)) return;
    std::memcpy(&g_partnerState, frame.payload.data(), sizeof(g_partnerState));
    g_partnerStateSlot = frame.slot;
    g_partnerStateAt = std::chrono::steady_clock::now();
}

void onPeerJoined() {
    g_partnerStateSlot = -1;
    g_maxHp = partner_status::kBaseMaxHp;
}

}  // namespace partner_hud
