#pragma once
// DS2-internal: the partner's body as a second focus for the NPC managers' activity update (see ds2/sneaking_focus.cpp).
namespace sneaking_focus {

// Start-up: patches the distance computation of the NPC managers' shared update body 0x141bd3d50. `diagnostics`
// (adapter.ini diagnostics=1) also logs the managers' groups (members, budget, radii) every 5 s; never on in a shipped config.
void installEarly(bool diagnostics);

}  // namespace sneaking_focus
