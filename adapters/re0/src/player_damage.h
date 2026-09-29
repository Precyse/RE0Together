#pragma once

#include <cstdint>

#include "net_client.h"
#include "protocol.h"

// Player HP and death ownership: only a character's owner changes its HP or kills it. Remote deaths arrive as
// PLAYER_DIED and replay through the game's own death handler.
namespace player_damage {

constexpr uint16_t kMsgPlayerDied = proto::kFirstGameType + 0x20;  // 0x0120, reliable: u8 character id

bool install(NetClient& net);

void uninstall();

// Sets any character's HP (player or enemy) through the game's setter, bypassing the ownership gate.
void setHp(uintptr_t character, int32_t hp);

// Net thread: queues a PLAYER_DIED for the game thread.
void onFrame(const GameFrame& frame);

}  // namespace player_damage
