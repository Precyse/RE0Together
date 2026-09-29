#pragma once
#include <cstdint>

#include "net_client.h"
#include "protocol.h"

// Shared camera: both machines focus the same character. The host decides; while a peer is connected the adapter
// owns switching: a local V press becomes a request the host applies (directly on the host itself). The host also
// focuses its own character, Rebecca, whenever the player objects change.
namespace camera_parity {

constexpr uint16_t kMsgSwitchRequest = proto::kFirstGameType + 3;  // 0x0103, guest to host, reliable: u8 character id

// Net thread: remembers the host's controlled character (PLAYER_STATE) and, on the host, a guest's switch request.
void onFrame(const GameFrame& frame);

// The local player pressed the switch key (net thread). Asks for the character that is not focused.
void onLocalSwitchKey();

// This machine just moved the focus to its own character (game thread): parity waits for the host to agree
// instead of switching back.
void holdLocalFocus();

// Registers the per-frame switch handling.
void enable(NetClient& net);

}  // namespace camera_parity
