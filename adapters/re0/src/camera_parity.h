#pragma once
#include <cstdint>

#include "net_client.h"
#include "protocol.h"

// The camera. TEAM together: both machines focus the same character; the host decides, a local V press becomes a request
// the host applies (directly on the host itself), and the host focuses its own character, Rebecca, whenever the player
// objects change. Independent play (split_rooms::independent): each machine keeps its own character focused and V
// does nothing. While a peer is connected the game never sees the keyboard switch key (command_input), so only the
// adapter switches.
namespace camera_parity {

constexpr uint16_t kMsgSwitchRequest = proto::kFirstGameType + 3;  // 0x0103, guest to host, reliable: u8 character id

// Net thread: remembers the host's controlled character (PLAYER_STATE) and, on the host, a guest's switch request.
void onFrame(const GameFrame& frame);

// The local player pressed the switch key (net thread). Asks for the character that is not focused.
void onLocalSwitchKey();

// This machine just moved the focus to its own character (game thread): a guest's parity waits for the host to agree
// instead of switching back.
void holdLocalFocus();

// Registers the per-frame switch handling.
void enable(NetClient& net);

}  // namespace camera_parity
