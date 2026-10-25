#include "host_exit.h"

#include <atomic>
#include <cstdint>

#include "character_owner.h"
#include "debug_overlay.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "host_exit_rule.h"
#include "lobby.h"
#include "log.h"
#include "pose_wire.h"
#include "room_phase.h"

namespace {

constexpr float kToastSeconds = 6.0f;

std::atomic<bool> g_pending{false};
host_exit_rule::AwayWatch g_away;  // game thread

// Game thread: leaves once a character is controlled and the game is in plain gameplay (Main phase, no door, no menu),
// the state the game over's own exit starts from. Writing the phase field directly crashes the game: only the request.
void onTick() {
    if (game::controlled() == 0) {
        g_away.clear();
        return;
    }
    const bool guest = character_owner::linked() && !character_owner::isHost();
    if (guest && !g_pending && g_away.hostLeftGame(lobby::hostInGame(), pose_wire::clockMs())) {
        logger::write("host_exit: the host's game left play (title, closed or crashed)");
        host_exit::request();
    }
    if (!g_pending || !game_state::playing()) return;
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
