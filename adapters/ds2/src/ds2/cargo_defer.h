#pragma once
// DS2-internal: deletions of the partner body's cargo pieces, carried out on the simulation thread (see ds2/cargo_defer.cpp).
namespace cargo_defer {

// Start-up: the simulation tick that runs the queued deletions.
void installEarly();

}  // namespace cargo_defer
