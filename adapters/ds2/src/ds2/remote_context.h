#pragma once
// DS2-internal: while a message is dispatched to the remote player's entity, the helpers that walk the PlayerManager
// for "the player with local index 0" (Sam) answer for the remote instead. The context is per thread.
#include <cstdint>

namespace remote_context {

// Around the entity message dispatcher (rcx = entity + 0x2D0): enter(handlers), the original dispatch, leave(result).
bool enter(uintptr_t handlerList);
void leave(bool remote);

// True on a thread that is dispatching to the remote entity.
bool active();

// Installs the helper redirects (early, from start-up).
void install();

}  // namespace remote_context
