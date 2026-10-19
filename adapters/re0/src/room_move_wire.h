#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

#include "character_owner.h"
#include "protocol.h"
#include "scene.h"
#include "spot_rule.h"

// Wire format of the native rooms (partner_mode=state, docs/RE0_NOTES.md "State mode: native rooms"): ROOM_MOVE tells
// the other machine which room a character just entered, DOOR_FOLLOW asks the partner's owner to go through the same
// door with its own character and DOOR_FOLLOW_RESULT answers. Pure, no game access, unit tested.
namespace room_move_wire {

constexpr uint16_t kMsgRoomMove = proto::kFirstGameType + 0x50;          // 0x0150, owner to all, reliable
constexpr uint16_t kMsgDoorFollow = proto::kFirstGameType + 0x51;        // 0x0151, door runner to the partner's owner, reliable
constexpr uint16_t kMsgDoorFollowResult = proto::kFirstGameType + 0x52;  // 0x0152, the partner's owner to the runner, reliable

// The engine asserts on a door entry above this (0x61ed50 indexes the entry spot table of 16 entries).
constexpr uint32_t kMaxEntry = 0xf;

// ROOM_MOVE `mode`: the entry spot mode (spot_rule.h) the owner's character stands on in the new room, or kAutoSpot when
// it was placed by a script or a join (no door spot), which leaves the choice to the receiver.
constexpr uint8_t kAutoSpot = 0xff;

// ROOM_MOVE: `seq` numbers the owner's moves; `loadSerial` is the owner's load serial once the new room is loaded.
struct RoomMove {
    uint32_t seq;
    uint16_t scene;
    uint16_t entry;
    uint16_t loadSerial;
    uint8_t character;
    uint8_t mode;
};
static_assert(sizeof(RoomMove) == 12);

// Why a door was followed: an ordinary door under TEAM, or a door a room script runs inside an event.
enum class Reason : uint8_t { Team, Script };
constexpr uint8_t kReasonCount = 2;

// DOOR_FOLLOW: the arguments of the runner's `sDoorLoad::start` and the room it starts from.
struct DoorFollow {
    uint32_t serial;
    uint16_t sourceScene;
    uint16_t scene;
    uint32_t entry;
    uint32_t type;
    uint32_t motion;
    uint32_t flag;
    uint8_t character;  // the runner's character
    uint8_t reason;     // Reason
    uint16_t reserved;
};
static_assert(sizeof(DoorFollow) == 28);

enum class Result : uint8_t { Commit, Busy, OtherRoom, NoPeer, Timeout };
constexpr uint8_t kResultCount = 5;

constexpr const char* name(Result result) {
    switch (result) {
        case Result::Commit: return "commit";
        case Result::Busy: return "busy";
        case Result::OtherRoom: return "other room";
        case Result::NoPeer: return "no peer";
        case Result::Timeout: return "timeout";
    }
    return "?";
}

// DOOR_FOLLOW_RESULT: `character` is the answering machine's character.
struct DoorFollowResult {
    uint32_t serial;
    uint8_t result;  // Result
    uint8_t character;
    uint16_t reserved;
};
static_assert(sizeof(DoorFollowResult) == 8);

template <class T>
std::optional<T> copyOut(std::span<const uint8_t> bytes) {
    if (bytes.size() != sizeof(T)) return std::nullopt;
    T value;
    std::memcpy(&value, bytes.data(), sizeof(T));
    return value;
}

inline bool sceneValid(uint32_t sceneId) { return sceneId < scene::kSceneIdLimit; }

// A ROOM_MOVE of the right size for a room and entry the game has, a known character and a real spot mode.
inline std::optional<RoomMove> decodeMove(std::span<const uint8_t> bytes) {
    const auto move = copyOut<RoomMove>(bytes);
    if (!move || !sceneValid(move->scene) || move->entry > kMaxEntry || move->loadSerial == 0 ||
        move->character >= character_owner::kCharacterCount ||
        (move->mode >= spot_rule::kModes && move->mode != kAutoSpot)) {
        return std::nullopt;
    }
    return move;
}

// A DOOR_FOLLOW of the right size between rooms the game has, with an entry it can place and a known character and
// reason. The door type and motion are the engine's own arguments and pass through as they are.
inline std::optional<DoorFollow> decodeFollow(std::span<const uint8_t> bytes) {
    const auto follow = copyOut<DoorFollow>(bytes);
    if (!follow || follow->serial == 0 || !sceneValid(follow->sourceScene) || !sceneValid(follow->scene) ||
        follow->entry > kMaxEntry || follow->flag > 0xff || follow->character >= character_owner::kCharacterCount ||
        follow->reason >= kReasonCount || follow->reserved != 0) {
        return std::nullopt;
    }
    return follow;
}

// A DOOR_FOLLOW_RESULT of the right size with a known result and character.
inline std::optional<DoorFollowResult> decodeResult(std::span<const uint8_t> bytes) {
    const auto result = copyOut<DoorFollowResult>(bytes);
    if (!result || result->serial == 0 || result->result >= kResultCount ||
        result->character >= character_owner::kCharacterCount || result->reserved != 0) {
        return std::nullopt;
    }
    return result;
}

}  // namespace room_move_wire
