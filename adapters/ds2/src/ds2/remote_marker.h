#pragma once
// DS2-internal: keeps the marker manager's single-marker groups (the player marker, the backpack marker) Sam's when
// the remote body exists (see ds2/remote_marker.cpp).
namespace remote_marker {

// Just before the remote is spawned: remembers which marker each group of one holds now (Sam's).
void snapshot();

// Simulation thread, every frame for a while after the spawn: where a group of one has been taken over by a marker the
// remote's entity or backpack created, takes that marker out of the manager's array and points the group back at Sam's.
void repair();

}  // namespace remote_marker
