#include "pose_capture.h"

#include <atomic>
#include <mutex>
#include <optional>

#include "character_owner.h"
#include "debug_stats.h"
#include "game_tick.h"
#include "join_sync.h"
#include "pose_capture_rule.h"
#include "pose_game.h"
#include "pose_wire.h"
#include "scene.h"
#include "state_net.h"

namespace {

bool g_installed = false;
std::atomic<uint32_t> g_poseSeq{0};  // the game thread samples, the net thread resends
uint32_t g_eventSeq = 0;
pose_capture_rule::SampleTimer g_sampleTimer;  // game thread only

std::mutex g_mutex;
pose_wire::Pose g_lastPose;  // guarded by g_mutex
bool g_hasPose = false;
pose_capture_rule::HeldResend g_heldResend;

// The object of the character this machine's player owns, or 0 without a peer.
uintptr_t ownedPlayer() {
    const character_owner::Character owned = character_owner::localCharacter();
    return owned == character_owner::Character::Unknown ? 0 : character_owner::find(owned);
}

void onMotionRequest(uintptr_t animControl, const pose_game::MotionRequest& request) {
    const uintptr_t player = ownedPlayer();
    if (!player || request.layer >= pose_wire::kMotionLayers) return;
    const std::optional<uint16_t> target = pose_game::controlTarget(player, animControl);
    if (!target) return;
    const pose_wire::MotionEvent event{pose_wire::clockMs(),
                                       request.flags,
                                       request.interp,
                                       request.start,
                                       ++g_eventSeq,
                                       static_cast<uint16_t>(request.motionNo),
                                       pose_wire::packLayer(static_cast<uint16_t>(request.layer), *target)};
    if (state_net::send(pose_wire::kMsgMotionEvent, true, proto::bytesOf(event))) {
        debug_stats::count(debug_stats::Counter::MotionEventsSent);
    }
}

void sendPose(const pose_wire::Pose& pose) {
    const pose_wire::PoseState wire = pose_wire::encode(pose);
    state_net::send(pose_wire::kMsgPoseState, false, proto::bytesOf(wire));
}

void afterMove(uintptr_t player) {
    if (player != ownedPlayer() || !join_sync::placed()) return;
    const uint32_t now = pose_wire::clockMs();
    if (!g_sampleTimer.due(now)) return;
    pose_wire::Pose pose;
    if (!pose_game::readPose(player, pose)) return;
    pose.senderMs = now;
    pose.seq = ++g_poseSeq;
    pose.room = scene::current();
    pose.loadSerial = scene::loadSerial();
    {
        std::lock_guard lock(g_mutex);
        g_lastPose = pose;
        g_hasPose = true;
        g_heldResend.onSample(now);
    }
    sendPose(pose);
}

}  // namespace

namespace pose_capture {

bool enable() {
    g_installed = pose_game::addAnimObserver(onMotionRequest);
    if (!g_installed) return false;
    game_tick::addPostMove("pose_capture", afterMove);
    return true;
}

void onNetTick() {
    const uint32_t now = pose_wire::clockMs();
    pose_wire::Pose pose;
    {
        std::lock_guard lock(g_mutex);
        // Unplaced (a new load still catching up), the last pose belongs to the previous game: never resent.
        if (!g_hasPose || !join_sync::placed() || !g_heldResend.due(now)) return;
        pose = g_lastPose;
    }
    pose.senderMs = now;
    pose.seq = ++g_poseSeq;
    pose.held = true;
    sendPose(pose);
}

void uninstall() {
    if (g_installed) pose_game::uninstallAnimHook();
}

}  // namespace pose_capture
