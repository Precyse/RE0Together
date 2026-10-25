#pragma once
#include <cstdint>

#include "net_client.h"
#include "save_load_rule.h"

// The save slot the session plays: the host's last loaded or saved slot. Every save made during a session goes to
// the dedicated co-op slot (the last player slot), so co-op progress never overwrites a solo save. The host announces it with its room phase
// (SAVE_SLOT 0x010F, {i32 slot, i32 phase}, reliable, on change and every 2 s); on the guest every load request from
// the menus is turned into that slot, so a guest can only ever load the host's game. With a launcher order
// (save_load_rule.h) whichever slot a player confirms in the load list loads the session's slot on either machine.
namespace session_slot {

constexpr int32_t kUnknown = save_load_rule::kUnknownSlot;

// Net thread: remembers the host's slot.
void onFrame(const GameFrame& frame);

// Net thread, every tick: the host announces its slot.
void onNetTick(NetClient& net);

// The host's slot on a guest, this machine's slot on the host, or kUnknown.
int32_t current();

// This game plays the host's session copy (a guest; stays true after the host left). Its saves are refused: they would
// land in the copy that is deleted when the launcher exits.
bool guestSession();

// Guest: the host is playing (its last announced phase is gameplay, not a title, game over or loading).
bool hostInGame();

// Hooks the save manager's load and save requests; `loadOrder` is the launcher's forced load (inactive: the game's load
// list decides). Call after the game code is decrypted.
bool enable(const save_load_rule::Order& loadOrder);

void uninstall();

}  // namespace session_slot
