#pragma once
// The shared locker (LOCKER_LIST, LOCKER_MOVE, LOCKER_RESULT; locker_wire.h): the host sends the lockers that changed, the
// whole set to a peer that joined or asked, and answers a guest's move; a guest sends the moves its own game made and
// follows the host's lists (ds2/lockers.cpp).
#include "net_client.h"

namespace locker_sync {

// Net thread.
void onFrame(NetClient& net, const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace locker_sync
