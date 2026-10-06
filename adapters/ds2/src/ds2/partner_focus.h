#pragma once
// DS2-internal: the partner's body is a second simulation focus on this machine (see ds2/partner_focus.cpp). The world
// around it stays active (enemies awake and fighting, physics, AI) the way it does around a second local player.
namespace partner_focus {

// Start-up: the detour on the engine's per-category activity update 0x140171d90. `diagnostics` (adapter.ini diagnostics=1)
// also logs the tables that keep members near the partner asleep and watches one sleeper's flags; never on in a shipped config.
void installEarly(bool diagnostics);

// Test toggle (test command partnerstream.txt "on"/"off", off by default): the update judges the partner's members as
// streamed in whatever the streaming manager says. Any thread.
void setStreamedInOverride(bool on);

}  // namespace partner_focus
