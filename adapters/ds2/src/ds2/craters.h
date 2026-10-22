#pragma once
// DS2-internal: the voidout craters of the DSEnvironmentManager (docs/DS2_NOTES.md, "Voidout craters"). The host reads its
// set and a guest follows it through the manager's own AddCrater and RemoveCrater; a guest's own catcher annihilation (the
// event that adds a crater and, in some missions, ends the game) is vetoed.
#include <cstdint>
#include <string>
#include <vector>

#include "crater_wire.h"

namespace craters {

// A 16-byte UUID as 32 hex digits (the text the log and the test commands use).
std::string uuidText(const uint8_t* uuid);

// Start-up: the detour on the catcher annihilation (0x141a05080) and the simulation-tick read and follow. They do nothing
// until setRole says host or guest.
void installEarly();

// The session role, set from the net thread each tick.
void setRole(bool host, bool guest);

// Host: the set read at the last poll (sorted by UUID), false until the first read. Any thread.
bool hostSet(std::vector<crater_wire::Crater>& out);

// Guest: from now on the local craters follow `set`. Call again with each newer CRATER_SET. Any thread.
void follow(std::vector<crater_wire::Crater> set);

// Simulation thread, for the test commands: logs every crater, adds one through AddCrater at `position` with the countdown
// of a catcher's crater, removes one through RemoveCrater. False when the manager or the call is not there; `remove` is
// also false for a crater that is not committed yet (new, or the world update paused by a menu).
void logAll();
bool add(const double (&position)[3], float radius, const uint8_t (&uuid)[crater_wire::kUuidSize]);
bool remove(const uint8_t (&uuid)[crater_wire::kUuidSize]);

}  // namespace craters
