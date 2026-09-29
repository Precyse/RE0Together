#pragma once
#include "net_client.h"

namespace input_record {

// Reads the pad vtable's original query functions. Call before the vtable tracer patches anything.
bool captureOriginals();

// Registers the per-frame recorder that sends the controlled player's pad as PAD_FRAME.
void enable(NetClient& net);

}  // namespace input_record
