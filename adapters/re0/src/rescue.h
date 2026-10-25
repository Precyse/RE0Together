#pragma once

// The rescue chord and the orphan-lock watchdog (all partner modes, a local recovery that sends nothing; rules in
// rescue_rule.h, docs/RE0_NOTES.md "Rescue chord and orphan lock"). Holding both stick clicks of a controller or F7 for
// 3 s is one press (command_input polls it). A scene's first press releases what holds the world, the second reloads the
// room through the door that led into it, later ones only say to quit and rejoin. Pause bits that stay set 4 s with no
// script running are released without a press.
namespace rescue {

// Registers the game tick step: the queued press, then the watchdog.
void enable();

// Any thread: one press of the chord. It also ends the peers' hold on this machine's world at once, since a held world does
// not tick; the ladder runs at the next game tick.
void press();

}  // namespace rescue
