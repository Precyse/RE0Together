#pragma once
#include <cstdint>

#include "net_client.h"

namespace door_travel {

// Wire payload of ROOM_STATE (0x0104), reliable, to all.
struct RoomState {
    uint16_t room;          // stage << 8 | room
    uint8_t partnerInRoom;  // the sender's partner character is in its loaded room
    uint8_t reserved;
};
static_assert(sizeof(RoomState) == 4);

// Net thread: remembers the driving peer's latest ROOM_STATE.
void onFrame(const GameFrame& frame);

// The room the peer last reported (ROOM_STATE), false before the first report.
bool peerRoom(uint16_t& out);

// Net thread: notes a door start even when the game stops ticking.
void onNetTick();

// Registers the per-frame door watcher: arrival broadcasts ROOM_STATE and forces a position check, ROOM_STATE repeats
// every 2 s, and a lasting room mismatch is reported. The partner itself travels by the game's follow logic.
void enable(NetClient& net);

}  // namespace door_travel
