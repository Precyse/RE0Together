#pragma once
#include <chrono>
#include <cstdint>

#include "live_menu_rule.h"
#include "room_phase.h"

// Pure rule (no game, unit tested): how long a peer's open screen holds this machine's world. Only a screen that stands
// the peer's own world still holds ours (live_menu_rule::Screen::Holding); a live menu (inventory, map) never does.
// Reading screens and cutscenes hold it until the peer is done; the pause menu, the save screen and menus with live
// menus off hold it for kHoldCap only, so a partner who walks away from the keyboard does not freeze the other player
// for good.
namespace menu_hold_rule {

using Clock = std::chrono::steady_clock;

constexpr auto kHoldCap = std::chrono::seconds(20);
constexpr auto kStale = std::chrono::seconds(6);  // MENU_STATE is resent every 2 s while open

struct PeerMenu {
    bool open = false;
    bool holding = false;                     // its world stands still (Screen::Holding)
    int32_t phase = room_phase::kUnreadable;  // the peer's room phase when it last said open
    Clock::time_point since;                  // when the current stretch of open (or of holding) began
    Clock::time_point seen;                   // the last MENU_STATE that said open
};

constexpr bool unlimited(int32_t phase) {
    return phase == room_phase::Message || phase == room_phase::MessageImm || phase == room_phase::EventDemo ||
           phase == room_phase::Movie;
}

inline bool isOpen(const PeerMenu& menu, Clock::time_point now) { return menu.open && now - menu.seen < kStale; }

inline void observe(PeerMenu& menu, live_menu_rule::Screen screen, int32_t phase, Clock::time_point now) {
    if (screen == live_menu_rule::Screen::None) {
        menu = PeerMenu{};
        return;
    }
    const bool holding = screen == live_menu_rule::Screen::Holding;
    if (!isOpen(menu, now) || (holding && !menu.holding)) menu.since = now;  // the cap counts held time only
    menu.open = true;
    menu.holding = holding;
    menu.phase = phase;
    menu.seen = now;
}

inline bool holds(const PeerMenu& menu, Clock::time_point now) {
    return isOpen(menu, now) && menu.holding && (unlimited(menu.phase) || now - menu.since < kHoldCap);
}

}  // namespace menu_hold_rule
