#pragma once
// DS2-internal: the partner's body as a second focus for the NPC managers' activity update (see ds2/sneaking_focus.cpp).
namespace sneaking_focus {

// Start-up: patches the distance computation of the NPC managers' shared update body 0x141bd3d50. `diagnostics`
// (adapter.ini diagnostics=1) also logs the managers' groups (members, budget, radii) every 5 s; never on in a shipped config.
void installEarly(bool diagnostics, bool testCommands);

// Test switches (test command npcpartner.txt, both off by default; the hook behind them is installed with test_commands=1 or
// diagnostics=1): `ownBudget` doubles every group's awake budget while the partner is live, `skipAcceptance` lets the partner's
// members pass the update's acceptance test. Any thread.
void setSwitches(bool ownBudget, bool skipAcceptance);

}  // namespace sneaking_focus
