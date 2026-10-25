#pragma once
#include "config.h"
#include "net_client.h"

// Outfits per player (docs/RE0_NOTES.md "Outfits"). The local player's adapter.ini outfit is stored at game start
// (before the room load builds the players) and reported to the peer as APPEARANCE; the partner's reported outfit is put
// on the partner's character the way the menu close swaps one (release the old set, request the new, rebuild). The
// outfits offered are appearance_rule's: never an opposite-sex outfit, never Rebecca as Wesker.
namespace appearance {

// Hooks the game start and registers the per-frame work. Call after character_owner::enable.
bool enable(NetClient& net, const Config& config);

// The model byte of the character's current outfit, appearance_rule::kNoOutfit while the game is not ready. Game thread.
uint8_t outfitOf(character_owner::Character character);

// The local player's outfit choice for a character (the Co-op page, through the launcher's LOBBY_STATE): put on at the
// next game start like the adapter.ini choice. appearance_rule::kNoOutfit keeps the save's outfit. Any thread.
void setChoice(character_owner::Character character, uint8_t model);

// Net thread: APPEARANCE from the owner of the character it names.
void onFrame(const GameFrame& frame);

void uninstall();

}  // namespace appearance
