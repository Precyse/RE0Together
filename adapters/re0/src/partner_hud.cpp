#include "partner_hud.h"

#include <algorithm>
#include <string>

#include "debug_stats.h"
#include "door_travel.h"
#include "menu_mirror.h"
#include "net_pad.h"
#include "partner_status.h"
#include "state_correction.h"
#include "state_sync.h"

namespace {

bool g_hostLeft = false;                          // net thread only
int g_maxHp = partner_status::kBaseMaxHp;         // the highest hp the partner reported this session
std::string g_shown;                              // the line the overlay has now

std::string peerName(const SessionSnapshot& session) {
    for (const PeerInfo& peer : session.peers) {
        if (peer.slot == net_pad::peerSlot()) return peer.name;
    }
    return "";
}

partner_status::Input gather(const SessionSnapshot& session) {
    partner_status::Input input;
    input.name = peerName(session);
    input.hostLeft = g_hostLeft;
    input.inMenu = menu_mirror::peerMenuOpen();
    input.sameRoom = door_travel::peerPlace() == door_travel::PeerPlace::Here;
    state_sync::PlayerState state;
    if (state_correction::latestState(state)) {
        g_maxHp = std::max(g_maxHp, static_cast<int>(state.hp));
        input.hasState = true;
        input.hp = state.hp;
        input.maxHp = g_maxHp;
        input.room = state.room;
    }
    return input;
}

}  // namespace

namespace partner_hud {

void onNetTick(const SessionSnapshot& session) {
    const std::string line = g_hostLeft || net_pad::active() ? partner_status::text(gather(session)) : "";
    if (line == g_shown) return;
    g_shown = line;
    debug_stats::setPartnerLine(line);
}

void onPeerJoined() {
    g_hostLeft = false;
    g_maxHp = partner_status::kBaseMaxHp;
}

void onHostLeft() { g_hostLeft = true; }

}  // namespace partner_hud
