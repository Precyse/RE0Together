#include "save_gate.h"

#include "door_travel.h"
#include "event_sync.h"
#include "net_pad.h"
#include "pose_wire.h"
#include "puppet.h"
#include "scene.h"

namespace save_gate {

save_gate_rule::Verdict check() {
    const int slot = net_pad::peerSlot();
    door_travel::RoomState room;
    const bool inDoor = door_travel::peerReport(room) && room.doorTarget != scene::kNone;
    std::optional<peer_sight::Sight> pose;
    if (slot >= 0) pose = puppet::latestPose(static_cast<uint8_t>(slot));
    return save_gate_rule::judge({pose, inDoor, event_sync::followsAny()}, pose_wire::clockMs());
}

}  // namespace save_gate
