#pragma once
// DS2-internal: the remote's pose follows another player's animation variables. The engine's states do not send state
// ids to the animation graph; they write named animation variables on the entity's AnimationManager and the graph picks
// its own states from them. Before the graph evaluates the remote (MsgGetAnimatedPose), every variable of the source is
// written into the remote's manager, so the remote copies the source's animation whatever its own states do. The
// source is the partner's reported variables (anim_sync), or the local player (loopback test).
#include <cstddef>
#include <cstdint>
#include <vector>

namespace remote_animation {

// One animation variable: its index in the manager's table, the engine's type (0 bool, 1 int, 2 float, 3 quat) and
// its value (1, 4 or 16 bytes by the type).
struct Change {
    uint16_t index;
    uint8_t type;
    uint8_t value[16];
};

// Bytes of a value of that type, 0 for a type that is not mirrored.
size_t valueBytes(uint8_t type);

void installEarly();

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
