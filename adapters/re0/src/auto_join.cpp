#include "auto_join.h"

#include <chrono>

#include "character_owner.h"
#include "game.h"
#include "log.h"
#include "net_pad.h"
#include "save_redirect.h"
#include "session_slot.h"
#include "virtual_keys.h"

namespace {

using Clock = std::chrono::steady_clock;

// Long enough for each logo, notice and menu to accept input, short enough that joining feels direct.
constexpr auto kConfirmInterval = std::chrono::milliseconds(2500);

bool g_driving = false;  // net thread only
Clock::time_point g_lastConfirm;

bool shouldDrive() {
    return net_pad::active() && !character_owner::isHost() && save_redirect::servingSession() &&
           session_slot::current() != session_slot::kUnknown && game::controlled() == 0;
}

}  // namespace

namespace auto_join {

void onNetTick() {
    const bool drive = shouldDrive();
    if (drive != g_driving) {
        g_driving = drive;
        virtual_keys::setRealKeyboardMuted(drive);
        logger::write(drive ? "auto_join: taking the guest into the host's game" : "auto_join: in game");
    }
    if (!drive) return;
    const auto now = Clock::now();
    if (now - g_lastConfirm < kConfirmInterval) return;
    g_lastConfirm = now;
    virtual_keys::tap(virtual_keys::kEnter);
}

}  // namespace auto_join
