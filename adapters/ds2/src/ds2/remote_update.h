#pragma once
// DS2-internal: what the remote body's DSPlayerComponent update runs. The update (message 0x14FF, thunk 0x14080ADE0 into
// 0x1407EEA70) steps the player's state machine, which is what starts a ride, so the remote needs it. Its motion call
// (0x1407F2570) first looks up the player's current ability and re-selects it, and for the remote that flickers Sam's use
// prompt, so the remote runs only the call's tail (the state machine's step).
namespace remote_update {

// Start-up, before the world loads.
void installEarly();

}  // namespace remote_update
