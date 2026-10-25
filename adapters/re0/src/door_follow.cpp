#include "door_follow.h"

#include <mutex>
#include <string>
#include <vector>

#include "character_owner.h"
#include "debug_overlay.h"
#include "debug_stats.h"
#include "event_sync.h"
#include "frame_router.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "log.h"
#include "net_pad.h"
#include "party_mode.h"
#include "pose_game.h"
#include "pose_wire.h"
#include "room_move_rule.h"
#include "room_move_wire.h"
#include "scene.h"
#include "state_net.h"

namespace {

using character_owner::Character;
using debug_stats::Counter;
using room_move_rule::FollowTracker;
using room_move_wire::DoorFollow;
using room_move_wire::DoorFollowResult;
using room_move_wire::Reason;
using room_move_wire::Result;

constexpr size_t kMaxQueued = 4;  // requests or results waiting for the game thread, the oldest dropped
constexpr float kToastSeconds = 3.0f;
constexpr uint8_t kNoThink = 0xff;  // a think group no free-movement rule accepts

struct Inbound {
    DoorFollow request;
    uint32_t arrivedMs;
};

std::mutex g_mutex;
std::vector<Inbound> g_requests;          // guarded by g_mutex
std::vector<DoorFollowResult> g_results;  // guarded by g_mutex

// Game thread only.
std::vector<Inbound> g_deferred;  // busy requests judged again next tick (room_move_rule::defers)
uint32_t g_lastSerial = 0;
FollowTracker g_tracker;
room_move_rule::ArrivalWatch g_arrival;
uint32_t g_followSerial = 0;  // the request the follow door in progress answers

template <class T>
void push(std::vector<T>& queue, const T& item) {
    std::lock_guard lock(g_mutex);
    if (queue.size() >= kMaxQueued) queue.erase(queue.begin());
    queue.push_back(item);
}

template <class T>
std::vector<T> drain(std::vector<T>& queue) {
    std::lock_guard lock(g_mutex);
    std::vector<T> out;
    out.swap(queue);
    return out;
}

uint32_t nextSerial() {
    if (++g_lastSerial == 0) ++g_lastSerial;
    return g_lastSerial;
}

// Net thread: the frame is the driving peer's and names a character that peer owns.
bool fromOwner(const GameFrame& frame, uint8_t character) {
    return static_cast<int>(frame.slot) == net_pad::peerSlot() &&
           character_owner::isOwnedBy(static_cast<Character>(character), frame.slot);
}

void onRequestFrame(const GameFrame& frame) {
    const auto request = room_move_wire::decodeFollow(frame.payload);
    if (!request || !fromOwner(frame, request->character)) {
        logger::writeUnlessRepeated("door_follow: request from slot %u refused", frame.slot);
        return;
    }
    push(g_requests, Inbound{*request, pose_wire::clockMs()});
}

void onResultFrame(const GameFrame& frame) {
    const auto result = room_move_wire::decodeResult(frame.payload);
    if (!result || !fromOwner(frame, result->character)) {
        logger::writeUnlessRepeated("door_follow: result from slot %u refused", frame.slot);
        return;
    }
    push(g_results, *result);
}

void sendResult(uint32_t serial, Result result, Character own) {
    const DoorFollowResult answer{serial, static_cast<uint8_t>(result), static_cast<uint8_t>(own), 0};
    state_net::send(room_move_wire::kMsgDoorFollowResult, true, proto::bytesOf(answer));
}

// What the follower's own character and the game look like right now.
room_move_rule::FollowContext contextFor(const DoorFollow& request) {
    const uintptr_t own = character_owner::localPlayer();
    pose_wire::Pose think;
    const bool thinks = own && pose_game::readThink(own, think);
    return {net_pad::active(),
            game_state::inCurrentRoom(own) && scene::current() == request.sourceScene,
            game_state::doorActive(),
            event_sync::leadsAny(),
            game_state::roomPhase(),
            game_state::menuOpen(),
            thinks ? think.group : kNoThink,
            thinks ? think.state : kNoThink};
}

// Runs the runner's door for the own character and waits for its room change.
void accept(const DoorFollow& request, Character own, uint32_t nowMs) {
    door_sync::runFollow({request.scene, request.entry, request.type, request.motion, request.flag,
                          static_cast<uint8_t>(own), 0, {}});
    g_arrival.arm(static_cast<uint16_t>(request.scene), nowMs);
    g_followSerial = request.serial;
    debug_stats::count(Counter::DoorFollowsAccepted);
    logger::write("door_follow: serial %u accepted", request.serial);
}

// `first` is false for a deferred request judged again.
void judge(const Inbound& inbound, uint32_t nowMs, bool first) {
    const DoorFollow& request = inbound.request;
    const Character own = character_owner::localCharacter();
    if (room_move_rule::requestStale(inbound.arrivedMs, nowMs)) {
        logger::write("door_follow: serial %u dropped, waited too long for a game tick", request.serial);
        return;
    }
    const Result verdict = room_move_rule::judge(static_cast<Reason>(request.reason), contextFor(request));
    if (room_move_rule::defers(verdict, inbound.arrivedMs, nowMs)) {
        if (first) logger::write("door_follow: serial %u busy here, waiting to follow", request.serial);
        g_deferred.push_back(inbound);
        return;
    }
    sendResult(request.serial, verdict, own);
    if (verdict == Result::Commit) {
        accept(request, own, nowMs);
        return;
    }
    debug_stats::count(Counter::DoorFollowsRejected);
    logger::write("door_follow: serial %u rejected (%s)", request.serial, room_move_wire::name(verdict));
}

// The partner's character stayed in its room (the follow request was refused or never answered).
void toastPartnerStayed() {
    const std::string text = std::string(character_owner::name(character_owner::other(character_owner::localCharacter()))) + " stayed behind.";
    debug_overlay::toast(text.c_str(), kToastSeconds, toast_queue::Style::Line);
}

void settle(const DoorFollowResult& answer) {
    const auto result = static_cast<Result>(answer.result);
    switch (g_tracker.onResult(answer.serial, result)) {
        case FollowTracker::Outcome::Ignored:
            logger::writeUnlessRepeated("door_follow: result for serial %u is not the pending request", answer.serial);
            break;
        case FollowTracker::Outcome::Accepted:
            logger::write("door_follow: serial %u partner accepted", answer.serial);
            break;
        case FollowTracker::Outcome::Rejected:
            logger::write("door_follow: serial %u partner rejected (%s)", answer.serial, room_move_wire::name(result));
            toastPartnerStayed();
            break;
    }
}

void forgetAll() {
    g_deferred.clear();
    g_tracker.clear();
    g_arrival = {};
    drain(g_requests);
    drain(g_results);
}

void onTick() {
    if (!net_pad::active()) {
        forgetAll();
        return;
    }
    const uint32_t now = pose_wire::clockMs();
    // Answers first: the runner's own door load stops this tick, and an answer that came during it is not a timeout.
    for (const DoorFollowResult& answer : drain(g_results)) settle(answer);
    if (const auto serial = g_tracker.expire(now)) {
        logger::write("door_follow: serial %u timed out", *serial);
        toastPartnerStayed();
    }
    if (g_arrival.expired(now)) logger::write("door_follow: serial %u never reached its room", g_followSerial);
    std::vector<Inbound> retry;
    retry.swap(g_deferred);
    for (const Inbound& inbound : retry) judge(inbound, now, false);
    for (const Inbound& inbound : drain(g_requests)) judge(inbound, now, true);
}

bool partnerInRoom(Character runner) {
    return game_state::inCurrentRoom(character_owner::find(character_owner::other(runner)));
}

}  // namespace

namespace door_follow {

bool enable() {
    if (!frame_router::add(room_move_wire::kMsgDoorFollow, frame_router::SenderPolicy::Any, onRequestFrame) ||
        !frame_router::add(room_move_wire::kMsgDoorFollowResult, frame_router::SenderPolicy::Any, onResultFrame)) {
        return false;
    }
    game_tick::addCallback("door_follow", onTick);
    return true;
}

void onLocalDoorStart(const door_sync::DoorChange& change) {
    const auto runner = static_cast<Character>(change.characterId);
    const room_move_rule::RunnerContext context{net_pad::active(), party_mode::current() == control_rule::PartyMode::Team,
                                                (change.flags & door_sync::kDoorAlone) != 0, partnerInRoom(runner)};
    const uint16_t source = scene::current();
    if (!room_move_rule::shouldRequest(context) || !room_move_wire::sceneValid(source) ||
        !room_move_wire::sceneValid(change.room) || change.entry > room_move_wire::kMaxEntry) {
        return;
    }
    const DoorFollow request{nextSerial(),
                             source,
                             static_cast<uint16_t>(change.room),
                             change.entry,
                             change.arg3,
                             change.arg4,
                             change.flag,
                             change.characterId,
                             static_cast<uint8_t>(room_move_rule::reasonFor(game_state::roomPhase())),
                             0};
    if (!state_net::send(room_move_wire::kMsgDoorFollow, true, proto::bytesOf(request))) return;
    g_tracker.begin(request.serial, pose_wire::clockMs());
    debug_stats::count(Counter::DoorFollowsSent);
    logger::write("door_follow: serial %u sent", request.serial);
}

uint8_t onOwnRoomChange(uint16_t sceneId, uint32_t entry) {
    if (!g_arrival.arrived(sceneId)) return static_cast<uint8_t>(game::kScenePlaceDoorMode);
    const auto spot = scene::placeBehind(character_owner::localPlayer(), entry);
    logger::write("door_follow: serial %u arrived in scene 0x%02x%s", g_followSerial, sceneId,
                  spot ? ", standing behind" : ", placement faulted");
    return static_cast<uint8_t>(spot ? *spot : game::kScenePlaceDoorMode);
}

}  // namespace door_follow
