#include "phase_watch.h"

#include "debug_stats.h"
#include "game_state.h"
#include "log.h"
#include "room_phase.h"

namespace {

int32_t g_lastPhase = room_phase::kUnreadable;  // net thread only

}  // namespace

namespace phase_watch {

void onNetTick() {
    const int32_t phase = game_state::roomPhase();
    if (phase == g_lastPhase) return;
    logger::write("phase: %s -> %s", room_phase::name(g_lastPhase), room_phase::name(phase));
    debug_stats::set(debug_stats::Gauge::RoomPhase, phase);
    g_lastPhase = phase;
}

}  // namespace phase_watch
