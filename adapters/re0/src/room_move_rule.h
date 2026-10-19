#pragma once
#include <cstdint>
#include <optional>

#include "room_move_wire.h"
#include "room_phase.h"
#include "scene.h"
#include "stream_clock.h"

// Pure rules of the native rooms (partner_mode=state, docs/RE0_NOTES.md "State mode: native rooms"): what a ROOM_MOVE
// does to the puppet, when the puppet's room is healed from its poses, when a door is followed and how the runner keeps
// track of its request. No game or network access, unit tested.
namespace room_move_rule {

using room_move_wire::Reason;
using room_move_wire::Result;

constexpr uint32_t kSelfHealMs = 1000;       // a pose room that differs from the puppet's record this long, with no ROOM_MOVE
constexpr uint32_t kFollowTimeoutMs = 8000;  // a request unanswered, or a follow door whose room never commits
constexpr uint8_t kFollowThinkGroup = 0;     // plain gameplay think group (walking, standing, aiming)
constexpr uint8_t kLastFollowThinkState = 6; // think group 0 states 0..6 are free movement; 7 and up are actions

// What a ROOM_MOVE means for the puppet, from the room it enters, the loaded room and the room it is in now.
enum class Kind : uint8_t { Leave, Enter, Between };

constexpr Kind classify(uint16_t target, uint16_t loaded, uint16_t current) {
    if (target == loaded) return Kind::Enter;
    return current == loaded ? Kind::Leave : Kind::Between;
}

constexpr const char* name(Kind kind) {
    switch (kind) {
        case Kind::Leave: return "leave";
        case Kind::Enter: return "enter";
        case Kind::Between: return "between";
    }
    return "?";
}

// The newest ROOM_MOVE not yet applied. A move numbered no later than one already taken in is dropped (wrap-safe).
class MoveInbox {
public:
    bool offer(const room_move_wire::RoomMove& move) {
        if (seen_ && !stream_clock::after(move.seq, lastSeq_)) return false;
        seen_ = true;
        lastSeq_ = move.seq;
        pending_ = move;
        return true;
    }

    const std::optional<room_move_wire::RoomMove>& pending() const { return pending_; }

    void consume() { pending_.reset(); }

    void clear() {
        seen_ = false;
        pending_.reset();
    }

private:
    bool seen_ = false;
    uint32_t lastSeq_ = 0;
    std::optional<room_move_wire::RoomMove> pending_;
};

// The puppet's record against the room the owner's poses name. A difference that lasts kSelfHealMs makes the puppet
// follow the poses (due), and again each kSelfHealMs while it lasts. A ROOM_MOVE restarts the wait.
class SelfHeal {
public:
    // `poseRoom` / `recordRoom`: the scene of the owner's newest pose and of the record the puppet is in (an id the game
    // does not have counts as unknown); `settled`: plain gameplay here (a door or an event moves characters itself).
    bool due(uint16_t poseRoom, uint16_t recordRoom, bool settled, uint32_t nowMs) {
        if (!settled || poseRoom >= scene::kSceneIdLimit || recordRoom >= scene::kSceneIdLimit || poseRoom == recordRoom) {
            armed_ = false;
            return false;
        }
        if (!armed_ || target_ != poseRoom) {
            armed_ = true;
            target_ = poseRoom;
            sinceMs_ = nowMs;
            return false;
        }
        if (nowMs - sinceMs_ < kSelfHealMs) return false;
        sinceMs_ = nowMs;
        return true;
    }

    void noteMove() { armed_ = false; }

private:
    bool armed_ = false;
    uint16_t target_ = scene::kNone;
    uint32_t sinceMs_ = 0;
};

// What the runner knows when its own door starts.
struct RunnerContext {
    bool linked;         // a peer is present
    bool team;           // party mode TEAM
    bool leftBehind;     // a room script left the partner behind for this door (TraceOff)
    bool partnerInRoom;  // the partner's character stands in the loaded room
};

constexpr bool shouldRequest(const RunnerContext& context) {
    return context.linked && context.team && !context.leftBehind && context.partnerInRoom;
}

// A door started during a cutscene or script event is a script's; any other is the player's own.
constexpr Reason reasonFor(int32_t roomPhase) { return room_phase::isEvent(roomPhase) ? Reason::Script : Reason::Team; }

// What the follower sees when a DOOR_FOLLOW is judged (all about its own character).
struct FollowContext {
    bool linked;
    bool inSourceRoom;  // its character stands in the loaded room and that is the request's source room
    bool doorRunning;
    bool leadsThread;   // it fired a room script thread that is still running
    int32_t roomPhase;
    bool menuOpen;
    uint8_t thinkGroup;
    uint8_t thinkState;
};

// Plain gameplay for a request: the Main phase, or for a script's door also the Event phase the script's own cutscene
// holds both players in.
constexpr bool plainEnough(Reason reason, int32_t roomPhase, bool menuOpen) {
    if (menuOpen) return false;
    return roomPhase == room_phase::Main || (reason == Reason::Script && roomPhase == room_phase::Event);
}

constexpr bool freeMovement(uint8_t group, uint8_t state) { return group == kFollowThinkGroup && state <= kLastFollowThinkState; }

constexpr Result judge(Reason reason, const FollowContext& context) {
    if (!context.linked) return Result::NoPeer;
    if (!context.inSourceRoom) return Result::OtherRoom;
    if (context.doorRunning || context.leadsThread || !plainEnough(reason, context.roomPhase, context.menuOpen) ||
        !freeMovement(context.thinkGroup, context.thinkState)) {
        return Result::Busy;
    }
    return Result::Commit;
}

// The runner's one outstanding DOOR_FOLLOW.
class FollowTracker {
public:
    enum class Outcome : uint8_t { Ignored, Accepted, Rejected };

    void begin(uint32_t serial, uint32_t nowMs) {
        serial_ = serial;
        sinceMs_ = nowMs;
        pending_ = true;
    }

    Outcome onResult(uint32_t serial, Result result) {
        if (!pending_ || serial != serial_) return Outcome::Ignored;
        pending_ = false;
        return result == Result::Commit ? Outcome::Accepted : Outcome::Rejected;
    }

    // The serial of the request that went unanswered for kFollowTimeoutMs (it is forgotten).
    std::optional<uint32_t> expire(uint32_t nowMs) {
        if (!pending_ || nowMs - sinceMs_ < kFollowTimeoutMs) return std::nullopt;
        pending_ = false;
        return serial_;
    }

    void clear() { pending_ = false; }

private:
    bool pending_ = false;
    uint32_t serial_ = 0;
    uint32_t sinceMs_ = 0;
};

// The follower's door has started; its character is placed behind the leader when the door's room change commits.
class ArrivalWatch {
public:
    void arm(uint16_t sceneId, uint32_t nowMs) {
        armed_ = true;
        scene_ = sceneId;
        sinceMs_ = nowMs;
    }

    // True once, when the room change into the awaited room commits.
    bool arrived(uint16_t sceneId) {
        if (!armed_ || sceneId != scene_) return false;
        armed_ = false;
        return true;
    }

    // True once, when the room change never came.
    bool expired(uint32_t nowMs) {
        if (!armed_ || nowMs - sinceMs_ < kFollowTimeoutMs) return false;
        armed_ = false;
        return true;
    }

private:
    bool armed_ = false;
    uint16_t scene_ = scene::kNone;
    uint32_t sinceMs_ = 0;
};

// A request that has waited kFollowTimeoutMs for a game tick is dropped: its runner gave up.
constexpr bool requestStale(uint32_t arrivedMs, uint32_t nowMs) { return nowMs - arrivedMs >= kFollowTimeoutMs; }

}  // namespace room_move_rule
