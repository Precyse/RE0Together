#pragma once
#include <cstdint>

#include "lobby_wire.h"

// Pure rules of the Co-op entry on the title screen's main menu (title_coop.cpp, unit tested). The title (uGUITitle)
// runs one state machine on +0x27c; its main menu has a fifth row the PC build never shows (+0x27a, the console
// "Online Store" row, whose choice sets state 0x2b and opens the Steam store overlay). The entry is that row: shown,
// relabelled (title_label.h), and its choice sent down the Options row's path (0x26 fade out, 0x27 open the Options
// screen, 0x28 wait for it to close, 0x1f main menu again), where the Options screen shows the native Co-op page.
namespace title_coop_rule {

// uGUITitle states (+0x27c); the dispatch handles 0..0x3a and returns on anything above.
constexpr uint32_t kStateMainMenuOpen = 0x1f;  // highlights the cursor row, then waits for input in 0x20
constexpr uint32_t kStateStoreChosen = 0x2b;   // the fifth row was chosen: the game would open the store overlay
constexpr uint32_t kStateOptionsFadeOut = 0x26;  // what the Options row's choice sets
constexpr uint32_t kLastGameState = 0x3a;
constexpr uint32_t kStateParked = 0x7f;  // the dispatch does nothing: no input read, no store, no game start
static_assert(kStateParked > kLastGameState);

constexpr uint8_t kFiveRows = 1;  // +0x27a: the main menu shows its fifth row

// The title state after the fifth row's choice: the Options path when the Co-op page will show there, else the main
// menu again (never the store).
constexpr uint32_t afterCoopChoice(bool pageReady) { return pageReady ? kStateOptionsFadeOut : kStateMainMenuOpen; }

constexpr uint32_t kStateMainMenuInput = 0x20;  // the main menu waits for input (its sequence and fade done)

// A title that starts at its main menu came back from play (the game over's exit and host_exit set 0xdcc131, the boot
// starts at 0 and a pause-menu quit at 6): in a co-op session it opens the Co-op page by itself, the way the Co-op row
// does, so both players land on it after a game over.
constexpr bool opensPageOnArrival(uint32_t firstState, bool inSession) { return inSession && firstState == kStateMainMenuOpen; }

// Where this game is for the other players while it shows its title.
constexpr lobby_wire::Place placeOf(bool pageActive, bool loading) {
    if (loading) return lobby_wire::Place::Loading;
    return pageActive ? lobby_wire::Place::Page : lobby_wire::Place::Title;
}

}  // namespace title_coop_rule
