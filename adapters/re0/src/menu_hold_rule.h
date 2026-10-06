#pragma once
#include <chrono>
#include <cstdint>

#include "room_phase.h"

// Pure rule (no game, unit tested): how long a peer's open screen holds this machine's world. Reading screens and
// cutscenes hold it until the peer is done; menus (inventory, options, pause, map, save) hold it for kHoldCap only, so
// a partner who walks away from the keyboard does not freeze the other player for good.
namespace menu_hold_rule {

using Clock = std::chrono::steady_clock;

constexpr auto kHoldCap = std::chrono::seconds(20);
constexpr auto kStale = std::chrono::seconds(6);  // MENU_STATE is resent every 2 s while open

struct PeerMenu {
    bool open = false;
    int32_t phase = room_phase::kUnreadable;  // the peer's room phase when it last said open
    Clock::time_point since;                  // when the current stretch of open began
    Clock::time_point seen;                   // the last MENU_STATE that said open
};

constexpr bool unlimited(int32_t phase) {
    return phase == room_phase::Message || phase == room_phase::MessageImm || phase == room_phase::EventDemo ||
           phase == room_phase::Movie;
}

inline bool isOpen(const PeerMenu& menu, Clock::time_point now) { return menu.open && now - menu.seen < kStale; }

inline void observe(PeerMenu& menu, bool open, int32_t phase, Clock::time_point now) {
    if (!open) {
        menu = PeerMenu{};
        return;
    }
    if (!isOpen(menu, now)) menu.since = now;
    menu.open = true;
    menu.phase = phase;
    menu.seen = now;
}

inline bool holds(const PeerMenu& menu, Clock::time_point now) {
    return isOpen(menu, now) && (unlimited(menu.phase) || now - menu.since < kHoldCap);
}

}  // namespace menu_hold_rule
