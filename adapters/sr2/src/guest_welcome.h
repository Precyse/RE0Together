#pragma once
#include <cstdint>

// Milestone 5h: a guest sends the objects it owns to a member that joins after it. In the shipped co-op only the host owned
// objects that existed before a member joined, so the callback for a member reaching the ready state queues the host's
// objects only on the host. With more players a guest owns such objects too (its player), and a later joiner never got
// them. The thunk runs the host's "queue every object I own" call, and the look sync, on the guests as well.
namespace guest_welcome {

// Under trace=1 each welcome is logged.
void setTrace(bool trace);

// G1: the guest branch of the member state change callback jumps here (edi = the remote member, ebx = 0).
uintptr_t thunk();

}  // namespace guest_welcome
