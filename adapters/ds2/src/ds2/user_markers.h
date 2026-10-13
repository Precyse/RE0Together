#pragma once
// DS2-internal: the local player's user markers (the map menu's Add User Marker, the in-world marker), the pings sent to the
// partner (docs/DS2_NOTES.md, "User markers"). The game keeps them in a fixed pool of 100 DSUserMarker objects in the marker
// manager; the simulation tick reads the whole pool, so every way a marker comes or goes (map, HUD, arrival, line removal,
// save load) is seen through one read.
#include <vector>

#include "ping_wire.h"

namespace user_markers {

// Start-up: the read every 250 ms on the simulation tick (in gameplay, world ready). With `diagnostics` also a log-only detour
// on the add (0x140c0ad80): the caller, the thread, the group, index and position of every marker the game adds.
void installEarly(bool diagnostics);

// The placed points as of the last read (sorted by id) when they differ from the set last handed out here; false when
// nothing changed. Any thread.
bool takeChangedSet(std::vector<ping_wire::Ping>& out);

// The placed points as of the last read; false before the first read. Any thread.
bool currentSet(std::vector<ping_wire::Ping>& out);

}  // namespace user_markers
