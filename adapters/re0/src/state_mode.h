#pragma once
#include "config.h"
#include "net_client.h"

// partner_mode=state: the one switch every state-mode module asks, and the one place that enables them.
namespace state_mode {

// Records the configured mode. Call once, before any module is enabled.
void configure(PartnerMode mode);

// True when adapter.ini asks for partner_mode=state. Any thread.
bool on();

// Enables every state-mode module (later phases append their enable call here) and logs how many came up.
// Call after configure(State), in the registration slot the state modules had in init.cpp.
void enableAll(NetClient& net);

}  // namespace state_mode
