#pragma once
// The weapon each player holds, mirrored on the partner's body (WEAPON_STATE / WEAPON_FIRE, weapon_wire.h). Each machine
// reports the weapon its local player has drawn when it changes and each shot it makes; the partner's body is given the
// same weapon and plays the same shots. Off unless adapter.ini says weapon_sync=1.
#include "net_client.h"

namespace weapon_sync {

void setEnabled(bool enabled);

// Net thread.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace weapon_sync
