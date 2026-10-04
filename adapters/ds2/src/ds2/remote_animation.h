#pragma once
// DS2-internal: the remote's pose follows another player's animation variables. The engine's states do not send state
// ids to the animation graph; they write named animation variables on the entity's AnimationManager and the graph picks
// its own states from them. Before the graph evaluates the remote (MsgGetAnimatedPose), every variable of the source is
// written into the remote's manager, so the remote copies the source's animation whatever its own states do. The
// source is the partner's reported variables (anim_sync), or the local player (loopback test).
#include <cstdint>
#include <vector>

#include "anim_change.h"

namespace remote_animation {

void installEarly();

// Any entity's animation manager (0 when it has none), and the variables of one: those that changed since `sent` (all
// of them when `snapshot`), which `sent` then remembers; and writing a set of values into a manager. The enemy puppets
// use these the way the remote body does. Simulation thread (the manager is read, and written before its graph
// evaluates).
uintptr_t managerOf(uintptr_t entity);
std::vector<Change> sampleChanges(uintptr_t manager, VariableValues& sent, bool snapshot);
void applyValues(uintptr_t manager, const VariableValues& values);
void applyChanges(uintptr_t manager, const std::vector<Change>& changes);

// The source the remote copies: the local player (loopback test).
void setMirrorLocalPlayer(bool enabled);
bool mirrorsLocalPlayer();

// Sending side: the local player's changed variables are sampled (30 Hz, on the game's update) while collecting.
void setCollecting(bool enabled);
void requestSnapshot();                    // the next sample reports every variable
std::vector<Change> takeLocalChanges();    // net thread: what was sampled since the last call

// Receiving side (net thread): the partner's reported value of one variable.
void setPeerChange(uint8_t slot, const Change& change);

}  // namespace remote_animation
