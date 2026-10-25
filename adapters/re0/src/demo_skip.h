#pragma once
#include <cstdint>

// Shared cutscene skip (partner_mode=state, docs/RE0_NOTES.md "State mode: scripts and cutscenes"). Both machines play a
// demo natively; one skip decides for both. The scheduler's update (0x54ac30) tests for a skip in state 5: a press on
// the follower becomes DEMO_SKIP_REQUEST to the thread's leader, whose scheduler then takes the skip; the leader's
// skip, from its own press or from the request, becomes DEMO_SKIP, and the follower's scheduler takes the same skip at
// its next state-5 frame (the fade-out and state 6 the engine's own skip branch sets, no input is synthesised).
namespace demo_skip {

// Registers DEMO_SKIP_REQUEST and DEMO_SKIP with the frame router and hooks the scheduler's update.
bool enable();

void uninstall();

// Game thread (the rescue): a demo plays (the scheduler updated in state 5 within the last 100 ms) and takes the engine's
// skip branch at its next update, whoever leads it; nothing is sent to the peer. False, and nothing happens, when no demo
// plays or the hook is not installed (it exists in state mode only).
bool skipLocal();

// Game thread: the thread with `serial` (`serialMine` as in event_sync) is about to run Demo_Start for demo `demoId`.
void onDemoStart(uint16_t demoId, uint16_t serial, bool serialMine, bool leads);

}  // namespace demo_skip
