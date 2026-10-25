#pragma once
#include "net_client.h"

// Feeds the HUD's partner tag (partner_status.h) from the session, the partner's last PLAYER_STATE, the room report and
// the menu mirror. The tag is empty unless both games are in play and the partner's state is fresh (partner_status::shows).
namespace partner_hud {

// Net thread, every tick: rebuilds the tag and hands it to the overlay when it changed.
void onNetTick(const SessionSnapshot& session);

// Net thread: keeps the partner's latest PLAYER_STATE (host and guest alike) for the tag's health and room.
void onFrame(const GameFrame& frame);

// A player joined: a new session starts and the partner's health history is forgotten.
void onPeerJoined();

}  // namespace partner_hud
