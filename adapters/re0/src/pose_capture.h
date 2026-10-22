#pragma once

// Owner side of partner_mode=state: the locally owned character is sampled after its own move (POSE_STATE, a 30 Hz time
// accumulator, unreliable) and every motion request its engine makes is sent as it was made (MOTION_EVENT, reliable,
// numbered). While no sample is taken for 250 ms (menu, door, load) the last one is resent at 4 Hz with the held flag.
namespace pose_capture {

// Hooks cAnimControl::set and registers the post-move sample. Call before game_tick::install, after MinHook is ready.
bool enable();

// Net thread, state mode only: the held resend of the last sample.
void onNetTick();

void uninstall();

}  // namespace pose_capture
