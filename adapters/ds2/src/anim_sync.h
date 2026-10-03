#pragma once
// The partner's animation: each machine reports the changes of its own player's animation variables (ANIM_STATE, at
// 30 Hz when something changed, and a full snapshot every second so a lost report heals), and the other machine writes
// them into its remote body (ds2/remote_animation) before the graph evaluates it. Variable indices are the same on
// both machines (the same entity resource), so a report is just index, type and value (anim_wire.h). Every report
// carries the sender's timestamp; pulses (a boolean that flips and comes back at once) are also sent reliably as
// ANIM_EVENT (anim_event.h).
#include "net_client.h"

namespace anim_sync {

// Net thread: the partner's reports, and sending the local player's changes.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace anim_sync
