#include "save_guard.h"

#include "funnels.h"
#include "link.h"
#include "log.h"
#include "save_hold_rule.h"
#include "story.h"
#include "troc_catalogue.h"
#include "world_gate.h"

namespace {

bool g_enabled = false;
save_hold_rule::Hold g_hold;
bool g_wasHolding = false;
save_guard::Stats g_stats;

uint32_t reasons() {
    uint32_t out = 0;
    if (story::replayWaiting()) out |= save_hold_rule::kReplayWaiting;
    const auto state = world_gate::state();
    if (state == world_gate_rule::State::Loading || state == world_gate_rule::State::AwaitLocalReady) out |= save_hold_rule::kLoading;
    return out;
}

}  // namespace

namespace save_guard {

bool resolveOpcodes(bool enabled) {
    const auto opcode = troc_catalogue::table().opcodeOf("SaveAutoReq");
    if (!opcode) return false;
    g_enabled = enabled;
    if (enabled) funnels::holdOpcode(*opcode);
    logger::write("save_guard: %s", enabled ? "on (autosaves held while a replay waits or a load runs)" : "off");
    return true;
}

void tick(uint64_t nowMs) {
    if (!g_enabled) return;
    const uint32_t why = link::isHost() ? 0 : reasons();
    const save_hold_rule::Step step = g_hold.tick(why, nowMs);
    if (step.abandonReplays) {
        story::abandonReplays();
        ++g_stats.bounded;
        logger::write("save_guard: hold reached 30 s (reasons 0x%X): waiting replays abandoned, autosaves released", why);
    }
    if (step.holdAutosaves && !g_wasHolding) ++g_stats.holds;
    g_wasHolding = step.holdAutosaves;
    funnels::setHolding(step.holdAutosaves);
    g_stats.heldSaves = static_cast<uint32_t>(funnels::heldCount());
}

Stats stats() { return g_stats; }

}  // namespace save_guard
