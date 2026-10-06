#pragma once
#include "net_client.h"

// Resync on demand: a guest asks the host for a new join snapshot (its room, both inventories, the full flag block and
// the floor changes), a host asks its guests to. Triggered by the file `coop\resync_now.txt` (created by hand, deleted
// when taken) and by a room desync that persists (door_travel). Only in a session with a peer.
namespace resync {

// Any thread: starts a resync (toast and log line with the reason).
void request(const char* reason);

// Net thread: the host asked this guest to resync.
void onFrame(const GameFrame& frame);

// Net thread, every tick: looks for resync_now.txt about once a second.
void onNetTick();

void enable(NetClient& net);

}  // namespace resync
