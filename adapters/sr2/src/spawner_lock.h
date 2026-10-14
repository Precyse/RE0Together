#pragma once

// Milestone 5i, section 9: the host crashed at 0x910E42 and a guest at 0x90D44E, reading a spawn group that was still
// being built. The group create (0x910370) links a new group into the active list first and fills its element slots after,
// and its callers (vehicle crew, the tick) are not all on the thread that walks the list under the tick lock 0x2ACDA08. The
// create now runs under that lock (recursive, so the tick's own path is unaffected), and a create that took the lock just now
// frees any group left with a null slot (a failed create) before it lets go.
namespace spawner_lock {

// Under trace=1 the creates are logged, at most one line per second.
void setTrace(bool trace);

// Hooks the entry of the group create (inside a hooks batch). True when the hook is queued.
bool installHook();

// Takes the hook out again.
void removeHook();

}  // namespace spawner_lock
