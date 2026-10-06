#pragma once
// The manual resync: creating the file <game>\coop\resync_now.txt makes this machine ask every peer for their full
// state and resend its own (resync::kAll). The adapter deletes the file when it acts on it.
#include "net_client.h"

namespace resync_trigger {

// Net thread, every tick; looks for the key every tick and for the file twice a second.
void poll(NetClient& net);

}  // namespace resync_trigger
