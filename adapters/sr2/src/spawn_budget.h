#pragma once

// Milestone 5i, section 12: the host crashed at 0x910E42 and 0x90D44E reading a null slot of an ambient spawn group. The
// group create 0x910370 links each group into the active list before it fills the group's element slots, and returns 0
// halfway when the static element pool (2400) runs dry, leaving the half-filled group linked. With 3 or more players the
// host streams chunks and crews around every remote, which empties that pool. A create now runs only when both pools can
// give everything it takes; otherwise it returns 0 before linking anything, as the game's own failure does.
namespace spawn_budget {

// Under trace=1 each refused create is logged (one line per second), and the partial groups found after a create.
void setTrace(bool trace);

// Hooks the group create (inside a hooks batch). True when the hook is queued.
bool installHook();

// Takes the hook out again.
void removeHook();

}  // namespace spawn_budget
