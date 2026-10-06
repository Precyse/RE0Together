#pragma once
// DS2-internal: the alert phase of enemy camps, read on the host and set on a guest (see ds2/camp_alert.cpp).
namespace camp_alert {

// Start-up: the simulation tick that polls (host) or applies (guest) the camps.
void installEarly();

// Test command: every camp goes to the alert phase through the game's own SetForceAlertCP. Simulation thread.
void alertAllCamps();

}  // namespace camp_alert
