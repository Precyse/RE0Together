#pragma once
// DS2-internal: what the remote body's DSPlayerComponent update runs. The update (message 0x14FF, thunk 0x14080ADE0 into
// 0x1407EEA70) starts the player's rides and states, but run for the remote it also flickers Sam's bottom-left prompts.
// Each of the calls it makes is hooked and can be skipped for the remote alone (a bit of the skip mask); the whole update
// can be skipped too. Test commands set the mask to find out which calls the ride start needs and which one writes the HUD.
#include <cstdint>

namespace remote_update {

constexpr uint32_t kSkipWholeUpdate = uint32_t{1} << 31;

// Test toggle: the mission-player notifications (MsgMissionPlayerNotify, MsgMissionPlayerVoiceEventNotify) sent while the
// remote's update runs are not dispatched. The mission or HUD side may take each one as "the player changed" and rebuild the
// prompt widget.
void setDropMissionNotify(bool drop);

// Whether the message with this vtable, sent now on this thread, must not be dispatched (see setDropMissionNotify).
bool dropsMessage(uintptr_t messageVtable);

// Start-up, before the world loads.
void installEarly();

// The calls skipped for the remote (bit i = call i, see the table in remote_update.cpp); 0 runs the whole update.
void setSkipMask(uint32_t mask);

}  // namespace remote_update
