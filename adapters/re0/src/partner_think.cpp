#include "partner_think.h"

#include <algorithm>

#include "character_owner.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "log.h"

namespace {

constexpr uint32_t kMinFramesBetweenApplications = 30;
constexpr uint32_t kPostDoorFrames = 30;

uint32_t g_frame = 0;
uint32_t g_lastApplied = 0;
bool g_applied = false;
uint32_t g_framesSinceDoor = kPostDoorFrames;  // saturates at kPostDoorFrames

bool applyDue() { return !g_applied || g_frame - g_lastApplied >= kMinFramesBetweenApplications; }

bool hasSubThink(uintptr_t player) {
    return game::readPointer(game::readPointer(player + game::kPlayerThinkOffset)) == game::kPartnerThinkVtable;
}

// Under co-op every character has a player-side control (Local, Remote or Locked): a cPlayerSubThink would follow
// partner AI instead of the owner's pad or a waiting stance, so it gets a full cPlayerThink.
bool coopControlled(uintptr_t player) {
    return character_owner::controlOf(character_owner::identify(player)) != character_owner::Control::Vanilla;
}

// A vanilla partner that still runs the cPlayerThink co-op gave it (the peer left) would read the local pad too.
bool needsPartnerAi(uintptr_t partner) {
    return partner && !coopControlled(partner) && !hasSubThink(partner);
}

bool doorSettled() {
    g_framesSinceDoor = game_state::doorActive() ? 0 : std::min(g_framesSinceDoor + 1, kPostDoorFrames);
    return g_framesSinceDoor >= kPostDoorFrames;
}

void onTick() {
    ++g_frame;
    if (!doorSettled() || !applyDue()) return;
    if (const uintptr_t partner = game::partner(); g_applied && needsPartnerAi(partner)) {
        game::setPartner(partner);
        g_lastApplied = g_frame;
        logger::write("partner_think: restored partner AI player=0x%x", static_cast<unsigned>(partner));
        return;
    }
    for (const uintptr_t player : {game::controlled(), game::partner()}) {
        if (!player || !coopControlled(player) || !hasSubThink(player)) {
            continue;
        }
        void* think = game::allocThink();
        game::constructPlayerThink(think);
        game::setThink(reinterpret_cast<void*>(player), think);
        g_applied = true;
        g_lastApplied = g_frame;
        logger::write("partner_think: applied player=0x%x think=0x%x", static_cast<unsigned>(player),
                      static_cast<unsigned>(reinterpret_cast<uintptr_t>(think)));
        return;
    }
}

}  // namespace

namespace partner_think {

void enable() { game_tick::addCallback("partner_think", onTick); }

}  // namespace partner_think
