#pragma once
#include "net_client.h"

// Feeds the overlay's partner status line (partner_status.h) from the session, the partner's last PLAYER_STATE, the
// room report and the menu mirror; remembers that the host left so a guest is told its game is not saved.
namespace partner_hud {

// Net thread, every tick: rebuilds the line and hands it to the overlay when it changed.
void onNetTick(const SessionSnapshot& session);

// Net thread: keeps the partner's latest PLAYER_STATE (host and guest alike) for the line's health and room.
void onFrame(const GameFrame& frame);

// A player joined: a new session starts, the host-left state and the partner's health history are forgotten.
void onPeerJoined();

// This guest's host left: the line says so until a player joins again (saves stay refused, see session_slot).
void onHostLeft();

}  // namespace partner_hud
