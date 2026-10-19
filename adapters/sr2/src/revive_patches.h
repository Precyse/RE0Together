#pragma once

// Milestone 16 W2 (docs/SR2_NOTES.md "Downed, revive and notoriety (M16 W2)"): with 3 or more players a downed player is revived by
// the nearest upright remote, a death of one remote no longer kills a downed local player another remote could revive, the respawn
// wait is kept per remote so the notoriety wipe needs every player down, and the notoriety witness, vehicle and guest lock rules
// count every remote. With one remote every rule gives the shipped answer.
namespace revive_patches {

// Writes the two sets of revive_sites.h, each all or nothing (inside a hooks batch); logs which went in. `trace` adds the trace
// lines of the decisions.
void install(bool trace);

}  // namespace revive_patches
