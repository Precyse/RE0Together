#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 17 wave 3 (docs/SR2_NOTES.md, "Trail Blazing (milestone 17 wave 3)"): the Trail Blazing activity counts every remote as it
// counts player 2: set on fire and put out, placed and asked out of its vehicle at the stage start, failed by water, lighting fire nodes
// and objects while riding, taking checkpoints, and shown the fire effect when near. Sites: trail_blazing_sites.h. Only installed with
// the milestone 7 activity logic in place.
namespace trail_blazing {

// The code behind a site of trail_blazing_sites.h; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bindingOf(partner_sites::Thunk thunk);

// Checks each set of sites (one that differs is logged with its first differing address and left out), then writes each set that
// matched, all or nothing. `trace` makes the thunks log what they decided.
void install(bool trace);

}  // namespace trail_blazing
