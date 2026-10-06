#pragma once
// DS2-internal: what the remote body's DSPlayerComponent update runs. The update (message 0x14FF, thunk 0x14080ADE0 into
// 0x1407EEA70) starts the player's rides and states, but run for the remote it also flickers Sam's bottom-left prompts.
// Each of the calls it makes is hooked and can be skipped for the remote alone (a bit of the skip mask); the whole update
// can be skipped too. Test commands set the mask to find out which calls the ride start needs and which one writes the HUD.
#include <cstdint>

namespace remote_update {

// Bit 9 (200) makes the motion call run whole for the remote; by default the remote runs only its state step.
constexpr uint32_t kSkipWholeUpdate = uint32_t{1} << 31;

// Bit 10 (400): the update's messages to Sam's entity are not dispatched (the use prompt is Sam's HUD state).
constexpr uint32_t kBlockMessagesToSam = uint32_t{1} << 10;

// Whether this thread is inside the remote's update.
bool running();

// Called by the message dispatcher for every message sent to `target` (an entity): counts what the remote's update sends
// (logged every few seconds), and returns true when the message must not be dispatched.
bool onMessage(uintptr_t messageVtable, uintptr_t target);

// Start-up, before the world loads.
void installEarly();

// The calls skipped for the remote (bit i = call i, see the table in remote_update.cpp); 0 runs the whole update.
void setSkipMask(uint32_t mask);

}  // namespace remote_update
