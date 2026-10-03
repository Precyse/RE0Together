#pragma once
// DS2-internal: gives the remote body its own baggage owners (see ds2/remote_baggage.cpp).
namespace remote_baggage {

// Start-up: the detour on the baggage carrier components' registration. Needs the remote body enabled.
void installEarly();

// Called just before the remote's character is spawned: for the next few seconds the carriers created for it get
// identifiers of their own.
void beginSpawn();

}  // namespace remote_baggage
