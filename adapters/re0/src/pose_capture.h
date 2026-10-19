#pragma once
#include "net_client.h"

// Owner side of partner_mode=state: the locally owned character is sampled after its own move (POSE_STATE, 30 Hz,
// unreliable) and every motion request its engine makes is sent as it was made (MOTION_EVENT, reliable, numbered).
namespace pose_capture {

// Hooks cAnimControl::set and registers the post-move sample. Call before game_tick::install, after MinHook is ready.
bool enable(NetClient& net);

void uninstall();

}  // namespace pose_capture
