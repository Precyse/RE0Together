#include "host_exit.h"

#include <atomic>
#include <cstdint>

#include "debug_overlay.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "log.h"
#include "room_phase.h"

namespace {

constexpr float kToastSeconds = 6.0f;

std::atomic<bool> g_pending{false};

// Game thread: leaves once a character is controlled and the game is in plain gameplay (Main phase, no door, no menu),
// the state the game over's own exit starts from. Writing the phase field directly crashes the game: only the request.
void onTick() {
    if (!g_pending || game::controlled() == 0) return;
    if (!game_state::playing()) return;
    g_pending = false;
    const uint8_t toTitleMenu = 1;
    game::writeMemory(game::kExitToTitleMenuFlag, toTitleMenu);
    game::requestRoomPhase(room_phase::Exit);
    logger::write("host_exit: the host left, leaving the world for the main menu");
}

}  // namespace

namespace host_exit {

void enable() { game_tick::addCallback("host_exit", onTick); }

void request() {
    debug_overlay::toast("Host left, returning to main menu.", kToastSeconds, toast_queue::Style::Panel);
    g_pending = true;
}

}  // namespace host_exit
