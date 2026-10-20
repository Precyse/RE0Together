#include "prop_sync.h"

#include <array>
#include <mutex>
#include <optional>

#include "character_owner.h"
#include "debug_stats.h"
#include "frame_router.h"
#include "game.h"
#include "game_tick.h"
#include "hooks.h"
#include "log.h"
#include "net_pad.h"
#include "pose_wire.h"
#include "prop_game.h"
#include "prop_wire.h"
#include "puppet.h"
#include "scene.h"
#include "state_net.h"

namespace {

namespace wire = prop_wire;
using debug_stats::Counter;
using prop_rule::Key;
using prop_rule::Pose;

using ObstacleMove = void(__fastcall*)(void* self, void* edx);

struct Answer {
    Key key;
    bool granted;
};

ObstacleMove g_original = nullptr;
std::mutex g_mutex;
prop_rule::LeaseTable g_leases;                             // guarded by g_mutex: the host's decisions, a guest's view
prop_rule::Journal g_journal;                               // guarded by g_mutex
std::array<prop_rule::Track, wire::kUnitCount> g_tracks;    // guarded by g_mutex: the other machine's boxes in the loaded room
std::optional<Answer> g_answer;                             // guarded by g_mutex: the host's answer, net thread to game thread
prop_rule::PushTracker g_push;                              // game thread
std::array<uintptr_t, wire::kUnitCount> g_targets{};        // game thread: the objects the tracks write, found each tick
uint16_t g_loadSerial = 0;                                  // game thread: the load serial the tracks belong to

uint32_t nowMs() { return pose_wire::clockMs(); }

Pose poseOf(const float (&pos)[3], float yaw) { return {{pos[0], pos[1], pos[2]}, yaw}; }

template <typename Message>
void fill(Message& message, const Key& key, const Pose& pose) {
    message.scene = key.scene;
    message.kind = wire::kObstacleKind;
    message.unitId = key.unit;
    for (size_t axis = 0; axis < pose.pos.size(); ++axis) message.pos[axis] = pose.pos[axis];
    message.yaw = pose.yaw;
}

const char* name(wire::LeaseOp op) {
    switch (op) {
        case wire::LeaseOp::Request: return "requested";
        case wire::LeaseOp::Grant: return "granted";
        case wire::LeaseOp::Deny: return "denied";
        case wire::LeaseOp::Release: return "released";
    }
    return "unknown";
}

void logLease(const Key& key, const char* what, int slot) {
    logger::write("prop: scene 0x%02x unit %u lease %s slot %d", key.scene, key.unit, what, slot);
}

void sendLease(const Key& key, wire::LeaseOp op, uint32_t serial, uint8_t holder) {
    const wire::Lease lease{serial, key.scene, key.unit, static_cast<uint8_t>(op), holder, {}};
    state_net::send(wire::kMsgLease, true, proto::bytesOf(lease));
}

void sendMove(const Key& key, const Pose& pose, bool reliable) {
    wire::Move move{};
    move.senderMs = nowMs();
    fill(move, key, pose);
    if (state_net::send(wire::kMsgMove, reliable, proto::bytesOf(move))) debug_stats::count(Counter::PropMovesSent);
}

// The shown time of the other machine's streams, or nothing before its first message.
std::optional<stream_clock::StreamClock> peerClock() {
    const int slot = net_pad::peerSlot();
    return slot < 0 ? std::nullopt : puppet::clockFor(static_cast<uint8_t>(slot));
}

// ---- Commits -----------------------------------------------------------------------------------------------------

// What a finished push means on this machine: the lease is over, the journal knows who pushed, and the other machine's
// box ends at the committed pose.
void applyCommit(const prop_rule::Record& record) {
    const std::optional<stream_clock::StreamClock> clock = peerClock();
    {
        std::lock_guard lock(g_mutex);
        g_leases.drop(record.key);
        g_journal.note(record);
        if (record.committer != character_owner::localSlot() && record.key.scene == scene::current()) {
            g_tracks[record.key.unit].finish(clock ? &*clock : nullptr, record.pose);
        }
    }
    debug_stats::count(Counter::PropCommits);
    logger::write("prop: unit %u committed (%.0f, %.0f, %.0f) serial %u", record.key.unit, record.pose.pos[0],
                  record.pose.pos[1], record.pose.pos[2], record.serial);
}

// Host: a push ended at `pose`; tell everyone.
void commit(const Key& key, uint8_t committer, uint32_t serial, const Pose& pose) {
    const prop_rule::Record record{key, serial, committer, pose};
    applyCommit(record);
    wire::Commit message{};
    message.serial = serial;
    fill(message, key, pose);
    message.committer = committer;
    state_net::send(wire::kMsgCommit, true, proto::bytesOf(message));
}

// ---- Leases ------------------------------------------------------------------------------------------------------

// Host: decides a request and tells the other machine when the holder is not it.
prop_rule::Decision decide(const Key& key, uint8_t slot) {
    prop_rule::Decision decision;
    {
        std::lock_guard lock(g_mutex);
        decision = g_leases.request(key, slot, nowMs());
        if (decision.granted && slot != character_owner::localSlot()) g_tracks[key.unit].clear();
    }
    logLease(key, decision.granted ? "granted" : "denied", slot);
    if (decision.granted && slot == character_owner::localSlot()) {
        sendLease(key, wire::LeaseOp::Grant, decision.serial, slot);
    } else if (slot != character_owner::localSlot()) {
        sendLease(key, decision.granted ? wire::LeaseOp::Grant : wire::LeaseOp::Deny, decision.serial, decision.holder);
    }
    return decision;
}

// Host: the holder `slot` let go of the box; what it had pushed to becomes the commit.
void releaseFromRemote(const Key& key, uint8_t slot) {
    std::optional<prop_rule::Held> held;
    {
        std::lock_guard lock(g_mutex);
        held = g_leases.release(key, slot);
    }
    if (!held) return;
    logLease(key, "released", slot);
    if (held->hasPose) commit(key, slot, held->serial, held->pose);
}

void onLease(const GameFrame& frame) {
    const auto lease = wire::decodeLease(frame.payload);
    if (!lease) {
        logger::writeUnlessRepeated("prop: refused a malformed lease message from slot %d", frame.slot);
        return;
    }
    const Key key{lease->scene, lease->unitId};
    const auto op = static_cast<wire::LeaseOp>(lease->op);
    const bool fromHost = character_owner::isHostSlot(frame.slot);
    if (op == wire::LeaseOp::Request || op == wire::LeaseOp::Release) {
        if (!character_owner::isHost() || fromHost) {
            logger::writeUnlessRepeated("prop: refused lease %s from slot %d (not for this machine)", name(op), frame.slot);
        } else if (op == wire::LeaseOp::Request) {
            decide(key, frame.slot);
        } else {
            releaseFromRemote(key, frame.slot);
        }
        return;
    }
    if (!fromHost) {
        logger::writeUnlessRepeated("prop: refused lease %s from slot %d (host only)", name(op), frame.slot);
        return;
    }
    const bool mine = lease->holder == character_owner::localSlot();
    {
        std::lock_guard lock(g_mutex);
        if (!mine) g_tracks[key.unit].clear();
        g_leases.adopt(key, lease->holder, lease->serial, nowMs());
        if (mine || op == wire::LeaseOp::Deny) g_answer = Answer{key, op == wire::LeaseOp::Grant && mine};
    }
    logLease(key, name(op), lease->holder);
}

// ---- Moves -------------------------------------------------------------------------------------------------------

void onMove(const GameFrame& frame) {
    const auto move = wire::decodeMove(frame.payload);
    if (!move) {
        logger::writeUnlessRepeated("prop: refused a malformed move from slot %d", frame.slot);
        return;
    }
    const Key key{move->scene, move->unitId};
    const std::optional<stream_clock::StreamClock> clock = peerClock();
    if (!clock || move->scene != scene::current()) {
        debug_stats::count(Counter::PropMovesRefused);
        return;
    }
    const Pose pose = poseOf(move->pos, move->yaw);
    std::lock_guard lock(g_mutex);
    const prop_rule::MoveResult result = g_leases.acceptMove(key, frame.slot, move->senderMs, pose, nowMs());
    if (result != prop_rule::MoveResult::Accepted) {
        debug_stats::count(Counter::PropMovesRefused);
        return;
    }
    g_tracks[key.unit].push(*clock, {move->senderMs, pose, false});
}

void onCommit(const GameFrame& frame) {
    const auto message = wire::decodeCommit(frame.payload);
    if (!message) {
        logger::writeUnlessRepeated("prop: refused a malformed commit from slot %d", frame.slot);
        return;
    }
    applyCommit({{message->scene, message->unitId}, message->serial, message->committer,
                 poseOf(message->pos, message->yaw)});
}

// ---- The local push ----------------------------------------------------------------------------------------------

// A push began: the host decides at once, a guest asks. Nobody to ask means nobody to arbitrate: the push is granted.
void requestLease(const Key& key) {
    if (character_owner::isHost()) {
        g_push.answered(key, decide(key, character_owner::localSlot()).granted);
        return;
    }
    sendLease(key, wire::LeaseOp::Request, 0, character_owner::localSlot());
    logLease(key, "requested", character_owner::localSlot());
}

// The push ended: the box's final pose goes out and the lease with it.
void endPush(const Key& key) {
    Pose pose;
    const uintptr_t obstacle = prop_game::obstacleOf(key.scene, key.unit);
    const bool known = obstacle && prop_game::readPose(obstacle, pose);
    if (character_owner::isHost()) {
        std::optional<prop_rule::Held> held;
        {
            std::lock_guard lock(g_mutex);
            held = g_leases.release(key, character_owner::localSlot());
        }
        if (held && known) commit(key, character_owner::localSlot(), held->serial, pose);
        return;
    }
    if (known) sendMove(key, pose, true);
    sendLease(key, wire::LeaseOp::Release, 0, character_owner::localSlot());
}

void cancelRequest(const Key& key) {
    if (!character_owner::isHost()) sendLease(key, wire::LeaseOp::Release, 0, character_owner::localSlot());
}

void takeAnswer() {
    std::optional<Answer> answer;
    {
        std::lock_guard lock(g_mutex);
        answer.swap(g_answer);
    }
    if (answer) g_push.answered(answer->key, answer->granted);
}

prop_rule::Observation observeLocalPush() {
    prop_rule::Observation now;
    now.nowMs = nowMs();
    const uintptr_t player = character_owner::localPlayer();
    const uint16_t sceneId = scene::current();
    if (!player || sceneId == scene::kNone) return now;
    const auto push = prop_game::pushOf(player, sceneId);
    if (!push || !prop_game::readPose(push->obstacle, now.pose)) return now;
    now.pushing = true;
    now.key = {sceneId, push->unit};
    return now;
}

void tickLocalPush() {
    takeAnswer();
    const prop_rule::Observation now = observeLocalPush();
    const prop_rule::Step step = g_push.observe(now);
    if (step.request) requestLease(step.key);
    if (step.timedOut) logLease(step.key, "timed out", character_owner::localSlot());
    if (step.move) {
        sendMove(step.key, step.pose, false);
        std::lock_guard lock(g_mutex);
        g_leases.touch(step.key, character_owner::localSlot(), now.nowMs);
    }
    if (step.end) endPush(step.key);
    if (step.cancel) cancelRequest(step.key);
}

// ---- The other machine's boxes -----------------------------------------------------------------------------------

// A room loaded: its boxes start from the flags' layout again, so the streams and commits of the old load are over.
void followRoom() {
    const uint16_t serial = scene::loadSerial();
    if (serial == g_loadSerial) return;
    g_loadSerial = serial;
    std::lock_guard lock(g_mutex);
    for (prop_rule::Track& track : g_tracks) track.clear();
    g_journal.forgetScene(scene::current());
}

// The host commits what a lapsed remote holder had pushed; a lapsed lease nobody committed just ends.
void expireLeases() {
    std::vector<prop_rule::Held> gone;
    {
        std::lock_guard lock(g_mutex);
        gone = g_leases.expire(nowMs());
    }
    for (const prop_rule::Held& held : gone) {
        logLease(held.key, "expired", held.holder);
        if (character_owner::isHost() && held.holder != character_owner::localSlot() && held.hasPose) {
            commit(held.key, held.holder, held.serial, held.pose);
        }
    }
}

void findTargets() {
    std::array<bool, wire::kUnitCount> live{};
    {
        std::lock_guard lock(g_mutex);
        for (size_t unit = 0; unit < g_tracks.size(); ++unit) live[unit] = !g_tracks[unit].empty();
    }
    const uint16_t sceneId = scene::current();
    for (uint8_t unit = 0; unit < wire::kUnitCount; ++unit) {
        g_targets[unit] = live[unit] && sceneId != scene::kNone ? prop_game::obstacleOf(sceneId, unit) : 0;
    }
}

void onTick() {
    followRoom();
    tickLocalPush();
    expireLeases();
    findTargets();
}

// Writes the pose the other machine's pusher has the box at, right before the box's own update.
void showTrack(uint8_t unit, uintptr_t obstacle) {
    const std::optional<stream_clock::StreamClock> clock = peerClock();
    if (!clock) return;
    prop_rule::Shown shown;
    {
        std::lock_guard lock(g_mutex);
        shown = g_tracks[unit].render(*clock, nowMs());
        if (shown.finished) g_tracks[unit].clear();
    }
    if (!shown.valid) return;
    prop_game::writePose(obstacle, shown.pose);
    if (shown.finished) g_targets[unit] = 0;
    debug_stats::count(Counter::PropMovesApplied);
}

void __fastcall obstacleMoveDetour(void* self, void* edx) {
    const auto obstacle = reinterpret_cast<uintptr_t>(self);
    for (uint8_t unit = 0; unit < wire::kUnitCount; ++unit) {
        if (g_targets[unit] == obstacle) {
            showTrack(unit, obstacle);
            break;
        }
    }
    g_original(self, edx);
}

}  // namespace

namespace prop_sync {

bool enable() {
    if (!frame_router::add(wire::kMsgLease, frame_router::SenderPolicy::Any, onLease) ||
        !frame_router::add(wire::kMsgMove, frame_router::SenderPolicy::Any, onMove) ||
        !frame_router::add(wire::kMsgCommit, frame_router::SenderPolicy::HostOnly, onCommit)) {
        return false;
    }
    game_tick::addCallback("prop_sync", onTick);
    return hooks::install("uObstacle::move", game::kObstacleMoveFunction, reinterpret_cast<void*>(&obstacleMoveDetour),
                          reinterpret_cast<void**>(&g_original));
}

void uninstall() { hooks::remove(game::kObstacleMoveFunction); }

event_rule::PropOwner triggerOwner(uint16_t sceneId, uint8_t unit) {
    const Key key{sceneId, unit};
    std::lock_guard lock(g_mutex);
    std::optional<uint8_t> slot = g_leases.holderOf(key, nowMs());
    if (!slot) slot = g_journal.lastCommitter(key);
    return prop_rule::ownerOf(slot, character_owner::localSlot());
}

std::vector<prop_rule::Record> commits() {
    std::lock_guard lock(g_mutex);
    return g_journal.records();
}

void adoptCommits(const std::vector<prop_rule::Record>& records) {
    for (const prop_rule::Record& record : records) applyCommit(record);
}

}  // namespace prop_sync
