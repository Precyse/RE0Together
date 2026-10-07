#include "partner_hud.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "debug_stats.h"
#include "door_travel.h"
#include "menu_mirror.h"
#include "partner_status.h"
#include "state_sync.h"

namespace {

bool g_hostLeft = false;                          // net thread only
int g_maxHp = partner_status::kBaseMaxHp;         // the highest hp the partner reported this session
std::string g_shown;                              // the line the overlay has now
state_sync::PlayerState g_partnerState{};          // net thread only: the partner's latest PLAYER_STATE
int g_partnerStateSlot = -1;                      // the slot it came from, -1 before the first

// The other player in the session, on the host and on the guest alike.
const PeerInfo* partner(const SessionSnapshot& session) {
    for (const PeerInfo& peer : session.peers) {
        if (peer.slot != session.localSlot) return &peer;
    }
    return nullptr;
}

partner_status::Input gather(const PeerInfo& peer) {
    partner_status::Input input;
    input.name = peer.name;
    input.hostLeft = g_hostLeft;
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
    const PeerInfo* peer = partner(session);
    std::string line;
    if (peer) line = partner_status::text(gather(*peer));
    else if (g_hostLeft) line = partner_status::text(gather(PeerInfo{}));
    if (line == g_shown) return;
    g_shown = line;
    debug_stats::setPartnerLine(line);
}

void onFrame(const GameFrame& frame) {
    if (frame.type != state_sync::kMsgPlayerState || frame.payload.size() != sizeof(state_sync::PlayerState)) return;
    std::memcpy(&g_partnerState, frame.payload.data(), sizeof(g_partnerState));
    g_partnerStateSlot = frame.slot;
}

void onPeerJoined() {
    g_hostLeft = false;
    g_partnerStateSlot = -1;
    g_maxHp = partner_status::kBaseMaxHp;
}

void onHostLeft() { g_hostLeft = true; }

}  // namespace partner_hud
