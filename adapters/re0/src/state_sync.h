#pragma once
#include <cstdint>

#include "net_client.h"
#include "protocol.h"

namespace state_sync {

constexpr uint16_t kMsgPlayerState = proto::kFirstGameType;
constexpr float kSendHz = 60.0f;  // PLAYER_STATE rate; seq advances once per send

// Wire payload of PLAYER_STATE (0x0100).
struct PlayerState {
    uint32_t seq;
    float pos[3];
    float quat[4];
    uint8_t characterId;         // the sender's OWNED character (host Rebecca, guest Billy): 0 Billy, 1 Rebecca
    uint8_t senderIsHost;
    uint16_t room;               // sender's loaded scene id (scene.h)
    int32_t hp;                  // HP of the owned character
    uint8_t focusedCharacterId;  // the sender's camera character (sPlayer+0x2c), 0xFF when unknown
    uint8_t reserved[3];
    uint16_t motion;             // the owned character's current motion number (uModel mMotionNo)
    uint16_t reserved2;
    float motionFrame;           // current frame of that motion
};
static_assert(sizeof(PlayerState) == 52);

// Starts the net client and, at 30 Hz on its thread, broadcasts the state of the character the local player owns.
void start(NetClient& net, uint16_t port);

}  // namespace state_sync
