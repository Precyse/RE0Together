#pragma once
// DS2-internal: warp to the partner (hotkey F6): moves the local player beside the partner's reported position (see
// ds2/warp.cpp).
namespace warp {

// Start-up: the simulation tick that carries a requested warp out.
void installEarly();

// Render thread, once per frame: reads the hotkey.
void poll();

}  // namespace warp
