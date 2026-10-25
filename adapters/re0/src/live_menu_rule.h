#pragma once
#include <cstdint>

#include "room_phase.h"

// Pure rule (no game, unit tested): which screens keep the world running while a peer is connected (live menus,
// docs/RE0_NOTES.md "Live menus"). The inventory (also its files, options, map and the item pickup prompt), the map and
// a message window opened from plain gameplay (examine text, a locked door, an item's text, the "cannot change" notice)
// stop the world with sUnit stop reasons the phase sets on entry and clears on exit; live menus take those back. The
// pause menu, the save screen, cutscenes and the messages of a scripted event keep holding the world.
namespace live_menu_rule {

// What a local screen does to the world. The values are MENU_STATE's `open` byte (an older build sends 0 or 1).
enum class Screen : uint8_t {
    None = 0,     // no screen: plain gameplay
    Holding = 1,  // the world stands still here, and the peer's world in the same room is held too (menu_hold_rule)
    Live = 2,     // a menu is open while the world keeps running on both machines
};

// sUnit stop reasons the screens set (setStopReason 0x652ce0; group masks from the table 0xdb5ef0): SubScreen enter
// 0x60bf20 and Map enter 0x60a530 set 7 and 8, cRoomPhaseMessage v6 0x60a8a0 sets 7 and 12, cRoomPhaseMessageImm v6
// 0x60a9f0 sets 9; each phase's exit (v8) clears its own.
constexpr uint32_t kStopScreen = 7;       // groups 0xbe00
constexpr uint32_t kStopScreenMore = 8;   // groups 0x13800000
constexpr uint32_t kStopNotice = 9;       // groups 0x1880be80
constexpr uint32_t kStopMessage = 12;     // groups 0x10800000

constexpr uint32_t bit(uint32_t reason) { return 1u << reason; }

// The stop reasons the screen of `phase` sets (a mask of reason bits), 0 for a phase that is never live.
constexpr uint32_t screenStops(int32_t phase) {
    switch (phase) {
        case room_phase::SubScreen:
        case room_phase::Map: return bit(kStopScreen) | bit(kStopScreenMore);
        case room_phase::Message: return bit(kStopScreen) | bit(kStopMessage);
        case room_phase::MessageImm: return bit(kStopNotice);
        default: return 0;
    }
}

// The local screen as the game shows it.
struct View {
    int32_t phase = room_phase::kUnreadable;
    int32_t beneath = room_phase::kUnreadable;  // the phase the current one was pushed over (phase manager stack)
    bool subMenuOpen = false;                   // sSubMenu is not closed
    bool paused = false;                        // the pause menu (sGamePause, stop reason 6) is up
    bool scriptEvent = false;                   // sGameInfo +0x50 holds a scripted event's pause bits
};

// A message is the player's own (live) when the script op or the Main phase pushed it over plain gameplay; one pushed
// over an event or a cutscene, or shown while a scripted event holds its pause bits, is part of that event.
constexpr bool liveScreen(const View& view) {
    if (view.phase == room_phase::SubScreen || view.phase == room_phase::Map) return true;
    const bool message = view.phase == room_phase::Message || view.phase == room_phase::MessageImm;
    return message && view.beneath == room_phase::Main && !view.scriptEvent;
}

// `live`: live menus are on and a peer is connected.
constexpr Screen screenOf(const View& view, bool live) {
    if (view.paused) return Screen::Holding;
    if (live && liveScreen(view)) return Screen::Live;
    return view.subMenuOpen || room_phase::pausesWorld(view.phase) ? Screen::Holding : Screen::None;
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
