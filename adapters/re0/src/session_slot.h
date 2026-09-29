#pragma once
#include <cstdint>

#include "net_client.h"

// The save slot the session plays: the host's last loaded or saved slot. The host announces it with its room phase
// (SAVE_SLOT 0x010F, {i32 slot, i32 phase}, reliable, on change and every 2 s); on the guest every load request from
// the menus is turned into that slot, so a guest can only ever load the host's game.
namespace session_slot {

constexpr int32_t kUnknown = -1;

// Net thread: remembers the host's slot.
void onFrame(const GameFrame& frame);

// Net thread, every tick: the host announces its slot.
void onNetTick(NetClient& net);

// The host's slot on a guest, this machine's slot on the host, or kUnknown.
int32_t current();

// Guest: the host is playing (its last announced phase is gameplay, not a title, game over or loading).
bool hostInGame();

// Hooks the save manager's load and save requests. Call after the game code is decrypted.
bool enable();

void uninstall();

}  // namespace session_slot
