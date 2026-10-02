#pragma once
// DS2-internal: the remote's pose follows another player's animation variables. The engine's states do not send state
// ids to the animation graph; they write named animation variables on the entity's AnimationManager and the graph picks
// its own states from them. Before the graph evaluates the remote (MsgGetAnimatedPose), every variable of the source is
// written into the remote's manager, so the remote copies the source's animation whatever its own states do.
namespace remote_animation {

void installEarly();

// The source the remote copies: the local player (loopback test).
void setMirrorLocalPlayer(bool enabled);
bool mirrorsLocalPlayer();

}  // namespace remote_animation
