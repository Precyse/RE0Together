#include "pose_capture.h"

#include "character_owner.h"
#include "debug_stats.h"
#include "game_tick.h"
#include "join_sync.h"
#include "pose_game.h"
#include "pose_wire.h"
#include "scene.h"

namespace {

constexpr uint32_t kSampleEveryFrames = 2;  // the game moves a character 60 times a second: 30 Hz

NetClient* g_net = nullptr;
uint32_t g_frame = 0;
uint32_t g_poseSeq = 0;
uint32_t g_eventSeq = 0;

// The object of the character this machine's player owns, or 0 without a peer.
uintptr_t ownedPlayer() {
    const character_owner::Character owned = character_owner::localCharacter();
    return owned == character_owner::Character::Unknown ? 0 : character_owner::find(owned);
}

void onMotionRequest(uintptr_t animControl, const pose_game::MotionRequest& request) {
    const uintptr_t player = ownedPlayer();
    if (!player || animControl != pose_game::animControlOf(player) || request.layer >= pose_wire::kMotionLayers) return;
    const pose_wire::MotionEvent event{pose_wire::clockMs(),
                                       request.flags,
                                       request.interp,
                                       request.start,
                                       ++g_eventSeq,
                                       static_cast<uint16_t>(request.motionNo),
                                       static_cast<uint16_t>(request.layer)};
    if (g_net->send(pose_wire::kMsgMotionEvent, true, proto::kSlotAll, proto::bytesOf(event))) {
        debug_stats::count(debug_stats::Counter::MotionEventsSent);
    }
}

void afterMove(uintptr_t player) {
    if (player != ownedPlayer() || !join_sync::caughtUp() || ++g_frame % kSampleEveryFrames != 0) return;
    pose_wire::Pose pose;
    if (!pose_game::readPose(player, pose)) return;
    pose.senderMs = pose_wire::clockMs();
    pose.seq = ++g_poseSeq;
    pose.room = scene::current();
    const pose_wire::PoseState wire = pose_wire::encode(pose);
    g_net->send(pose_wire::kMsgPoseState, false, proto::kSlotAll, proto::bytesOf(wire));
}

}  // namespace

namespace pose_capture {

bool enable(NetClient& net) {
    g_net = &net;
    if (!pose_game::installAnimHook(onMotionRequest)) return false;
    game_tick::addPostMove("pose_capture", afterMove);
    return true;
}

void uninstall() {
    if (g_net) pose_game::uninstallAnimHook();
}

}  // namespace pose_capture
