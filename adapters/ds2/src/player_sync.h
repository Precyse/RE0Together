#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "net_client.h"
#include "protocol.h"

namespace player_sync {

constexpr uint16_t kMsgPlayerState = proto::kFirstGameType;  // 0x0100, unreliable, to all
constexpr float kSendHz = 60.0f;  // seq advances once per send

// Wire payload of PLAYER_STATE: where the sender's own player stands (world metres, Decima Z-up).
// Same leading fields as the RE0 message, so tools/echo_peer.py can echo and offset it.
struct PlayerState {
    uint32_t seq;
    float pos[3];
    float yaw;  // radians about the up axis
    uint32_t reserved;
};
static_assert(sizeof(PlayerState) == 24);

struct RemotePlayer {
    uint8_t slot = 0;
    std::string name;
    float position[3] = {};  // newest report extrapolated to now by the peer's velocity
    float yaw = 0;
};

// Starts the launcher link; its thread sends the local player at kSendHz and keeps the newest state per peer.
void start(NetClient& net, uint16_t port);

// Peers heard from recently, with the names the launcher reported for them.
std::vector<RemotePlayer> remotePlayers();

}  // namespace player_sync
