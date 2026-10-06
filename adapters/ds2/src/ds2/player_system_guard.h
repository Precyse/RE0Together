#pragma once
// DS2-internal: DSPlayerSystem (global 0x14623E9C8) holds singleton pointers (+0x2c90, +0x226520..+0x226610) that exactly two
// player components store in their init and clear in their removal handler, without checking whose they are:
// DSPlayerEquipmentManageComponent (init 0x140F69970, remove 0x140F7A930) and DSPlayerFacialRigManagerComponent (init
// 0x140E86A80, remove 0x140EE3EE0). The remote body is a second player entity, so its init overwrote Sam's pointers and
// removing it cleared them. The init must run (it builds the component's plugins), so every load of the system pointer
// inside the two inits is pointed at a cell: Sam's system for Sam's components, a scratch block for the remote's, whose
// stores then land nowhere. The removals are not dispatched to the remote's components (remote_guards). The equipment component's
// per-frame update (0x140F6B9B0) is not run for the remote either: it reads equipment records of the player state that the
// body's state does not have (a read of -1 at 0x140F75E12, intermittent at spawn).
namespace player_system_guard {

// Start-up, before the world loads.
void installEarly();

}  // namespace player_system_guard
