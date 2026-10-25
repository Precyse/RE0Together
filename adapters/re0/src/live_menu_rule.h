#pragma once
#include <cstdint>

#include "room_phase.h"

// Pure rule (no game, unit tested): which screens keep the world running while a peer is connected (live menus,
// docs/RE0_NOTES.md "Live menus"). The inventory (also its files, options, map and the item pickup prompt) and the map
// run in the SubScreen and Map phases; the game stops the world there with sUnit stop reasons 7 and 8, which live menus
// take back. The pause menu, the save screen, reading screens and cutscenes keep holding the world.
namespace live_menu_rule {

// What a local screen does to the world. The values are MENU_STATE's `open` byte (an older build sends 0 or 1).
enum class Screen : uint8_t {
    None = 0,     // no screen: plain gameplay
    Holding = 1,  // the world stands still here, and the peer's world in the same room is held too (menu_hold_rule)
    Live = 2,     // a menu is open while the world keeps running on both machines
};

constexpr bool liveScreen(int32_t phase) { return phase == room_phase::SubScreen || phase == room_phase::Map; }

// `paused`: the pause menu (sGamePause, stop reason 6) is up. `subMenuOpen`: sSubMenu is not closed.
// `live`: live menus are on and a peer is connected.
constexpr Screen screenOf(int32_t phase, bool subMenuOpen, bool paused, bool live) {
    if (paused) return Screen::Holding;
    if (live && liveScreen(phase)) return Screen::Live;
    return subMenuOpen || room_phase::pausesWorld(phase) ? Screen::Holding : Screen::None;
}

// Watches the local character's HP while a live menu is open. A drop means it was hurt: the menu then stops the world
// like the vanilla one until it closes, so no further hit, and no death, lands while its player cannot see.
class HurtWatch {
public:
    void update(Screen screen, int32_t hp) {
        if (screen != Screen::Live) {
            open_ = false;
            hurt_ = false;
            return;
        }
        if (open_ && hp < hp_) hurt_ = true;
        open_ = true;
        hp_ = hp;  // an item used in the menu raises it
    }

    // The world runs: a live menu is open and its character has not been hurt since it opened.
    bool releases() const { return open_ && !hurt_; }

private:
    bool open_ = false;
    bool hurt_ = false;
    int32_t hp_ = 0;
};

}  // namespace live_menu_rule
