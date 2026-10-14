#pragma once

// Milestone 5i, sections 9 and 10: the host crashed at 0x910E42 and a guest at 0x90D44E, reading a spawn group that was still
// being built or had been freed. The tick walks the active group list under the critical section 0x2ACDA08, but four game
// entries that create or free groups run on other threads without it: the group create 0x910370, the chunk parser 0x9140C0,
// the chunk unload 0x914900 and the vehicle crew free 0x912FD0. Each now runs under that lock (recursive, so the tick's own
// calls are unaffected).
namespace spawner_lock {

// Under trace=1 each entry is logged (one line per second per entry), the partial groups found after a group create, and
// any wait for the lock over 20 ms.
void setTrace(bool trace);

// Hooks the four entries (inside a hooks batch), all or none. True when the hooks are queued.
bool installHook();

// Takes the hooks out again.
void removeHook();

}  // namespace spawner_lock
