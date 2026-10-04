#pragma once
// DS2-internal: the host's BT regions and catcher activations, mirrored on a guest (see ds2/bt_events.cpp). Kept out of
// game.h so the BT work does not touch the shared engine interface.
#include <vector>

#include "bt_wire.h"

// TODO (open problem, docs/DS2_NOTES.md "BT and catcher sync"): a BT that grabs the guest's body on the host. On the
// host the guest is the remote body, an entity that is not Sam, and the catch sequence reads "the player". Best design:
// the grabbed player's own machine runs its own catch. The host detects the catcher's interaction target
// (DSCatcherInteractionAttachResource / DSPlayerCatcherAttachJointInfo) being the remote body, sends a new
// CatcherKind::GrabGuest {catcher entity UUID} to that guest and holds the remote body inert (its animation keeps
// arriving through ANIM_STATE); the guest replays the catch on its own Sam against its puppet of that catcher; the
// outcome (escape, or a voidout through the crater facts) returns to the host as facts. Not built until a live test
// shows what the host's catch does with the remote body.

namespace bt_events {

// Start-up: detours on SetBtActiveRegion, the catcher manager's territory activation and a locator's tar activation, and
// the simulation-tick appliers. They do nothing until setRole says host or guest.
void installEarly();

// The session role, set from the net thread each tick. A host reports; a guest vetoes the game's own BT region and
// catcher changes and follows the host's.
void setRole(bool host, bool guest);

// Host: the BT-active regions as the game holds them. False until the weather manager exists. Any thread.
bool readActiveRegions(bt_wire::BtEnv& out);

// Host: the catcher activations since the last call. Any thread.
std::vector<bt_wire::CatcherEvent> takeCatcherEvents();

// Guest: from now on the BT regions follow `env` (written through the game's own setter on the simulation thread; the
// guest's forecast is already pinned by WORLD_ENV). Call again with each newer BT_ENV. Any thread.
void followRegions(const bt_wire::BtEnv& env);

// Guest: replays the host's activation on the simulation thread; waits for the locator to be loaded here. Any thread.
void replayCatcher(const bt_wire::CatcherEvent& event);

}  // namespace bt_events
