#pragma once
// DS2-internal: the alert phase of enemy camps, read on the host and set on a guest (see ds2/camp_alert.cpp).
namespace camp_alert {

// Start-up: the simulation tick that polls (host) or applies (guest) the camps.
void installEarly();

}  // namespace camp_alert
