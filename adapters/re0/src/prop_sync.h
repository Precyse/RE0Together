#pragma once
#include <cstdint>
#include <vector>

#include "event_rule.h"
#include "prop_rule.h"

// Push blocks in partner_mode=state (docs/RE0_NOTES.md "State mode: props"). The character that pushes a box holds a
// lease on it, granted by the host; while it pushes it streams the box's pose (PROP_MOVE, 10 Hz while it moves) and when
// it lets go the host commits the pose the box rests at (PROP_COMMIT). The other machine writes the stream, a render
// delay late, into the box right before the box's own update. The persistent layout stays the story flags' (sObstacle).
namespace prop_sync {

// Registers PROP_LEASE, PROP_MOVE and PROP_COMMIT with the frame router, hooks the obstacle's update and registers the
// per-frame tick. False when a type or the hook could not be set up.
bool enable();

void uninstall();

// Any thread: who pushed the box `unit` of room `scene` last as this machine sees it: the lease holder, else the last
// committer, else nobody. The box's zone trigger fires on that machine.
event_rule::PropOwner triggerOwner(uint16_t scene, uint8_t unit);

// Any thread: the commits this machine knows (the newest per box); what a joining guest is told.
std::vector<prop_rule::Record> commits();

}  // namespace prop_sync
