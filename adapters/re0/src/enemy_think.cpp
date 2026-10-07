#include "enemy_think.h"

#include <array>
#include <chrono>

#include "debug_stats.h"
#include "enemy_net.h"
#include "enemy_registry.h"
#include "enemy_state.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "position_blend.h"
#include "scene.h"

namespace {

using enemy_follow_rule::Action;
using enemy_follow_rule::Decision;
using Clock = std::chrono::steady_clock;

constexpr auto kLogInterval = std::chrono::seconds(1);

using ThinkFunction = void(__fastcall*)(void* enemy, void* edx);
using SetActionFunction = void(__fastcall*)(void* enemy, void* edx, int32_t state, int32_t id, int32_t a, int32_t b);
ThinkFunction g_originalThink = nullptr;
SetActionFunction g_originalSetAction = nullptr;

// Game thread only.
std::array<enemy_follow_rule::PendingDecision, game::kEnemyPoolSlots> g_pending{};
uint16_t g_pendingRoom = scene::kNone;  // the room g_pending belongs to
uintptr_t g_dropSetActionOf = 0;        // the enemy whose think step is deciding for the owner right now
Clock::time_point g_lastApplyLog;

// Pending decisions name pool slots of one room load.
enemy_follow_rule::PendingDecision& pendingFor(uint8_t slot) {
    if (scene::current() != g_pendingRoom) {
        g_pending.fill({});
        g_pendingRoom = scene::current();
    }
    return g_pending[slot];
}

bool readAction(uintptr_t enemy, Action& out) { return game::readMemory(enemy + game::kEnemyActionOffset, out.word); }

void setAction(void* enemy, const Action& action) {
    g_originalSetAction(enemy, nullptr, action.word[0], action.word[1], action.word[2], action.word[3]);
}

// Puts the enemy on the pose the owner decided from when it is far off; the action about to start moves it from there.
void realign(uintptr_t enemy, uint8_t slot, const Decision& decision) {
    float pos[3];
    float quat[4];
    if (!game::readTransform(enemy, pos, quat)) return;
    const float drift = position_blend::distance(pos, decision.pos);
    if (!enemy_follow_rule::realigns(drift)) return;
    game::writeTransform(enemy, decision.pos, decision.quat);
    debug_stats::count(debug_stats::Counter::Snaps);
    logger::write("enemy_think: slot %u realigned at a decision, drift %.1f", slot, drift);
}

void logApplied(uint8_t slot, const Action& action) {
    const auto now = Clock::now();
    if (now - g_lastApplyLog < kLogInterval) return;
    g_lastApplyLog = now;
    logger::write("enemy_think: slot %u took the owner's decision (%d,%d,%d,%d)", slot, action.word[0], action.word[1],
                  action.word[2], action.word[3]);
}

// The follower's think step: it runs, its own choices are dropped, and the owner's newest decision is set instead.
void thinkForOwner(void* enemy, void* edx, uint8_t slot) {
    const uintptr_t self = reinterpret_cast<uintptr_t>(enemy);
    g_dropSetActionOf = self;
    g_originalThink(enemy, edx);
    g_dropSetActionOf = 0;
    Decision decision;
    if (!pendingFor(slot).take(decision)) return;
    realign(self, slot, decision);
    setAction(enemy, decision.action);
    debug_stats::count(debug_stats::Counter::EnemyDecisionsApplied);
    logApplied(slot, decision.action);
}

// The owner's think step: whatever record it chose goes to the peer with the pose it chose it from.
void thinkAsOwner(void* enemy, void* edx, int slot) {
    const uintptr_t self = reinterpret_cast<uintptr_t>(enemy);
    Decision decision;
    const bool reports = slot != enemy_registry::kNoSlot && enemy_state::leadsPeer() && readAction(self, decision.action);
    const Action before = decision.action;
    g_originalThink(enemy, edx);
    if (!reports || !readAction(self, decision.action) || decision.action == before ||
        !game::readTransform(self, decision.pos, decision.quat)) {
        return;
    }
    enemy_net::sendDecision(static_cast<uint8_t>(slot), decision);
}

void __fastcall thinkDetour(void* enemy, void* edx) {
    const int slot = enemy_registry::slotOf(reinterpret_cast<uintptr_t>(enemy));
    if (slot != enemy_registry::kNoSlot && enemy_state::followsOwner()) {
        return thinkForOwner(enemy, edx, static_cast<uint8_t>(slot));
    }
    thinkAsOwner(enemy, edx, slot);
}

void __fastcall setActionDetour(void* enemy, void* edx, int32_t state, int32_t id, int32_t a, int32_t b) {
    if (reinterpret_cast<uintptr_t>(enemy) == g_dropSetActionOf) return;
    g_originalSetAction(enemy, edx, state, id, a, b);
}

}  // namespace

namespace enemy_think {

bool install() {
    const bool setActionHooked =
        hooks::install("enemy setAction", game::kEnemyBaseSetActionFunction, reinterpret_cast<void*>(setActionDetour),
                       reinterpret_cast<void**>(&g_originalSetAction));
    const bool thinkHooked = setActionHooked &&
                             hooks::install("enemy think", game::kEnemyThinkFunction, reinterpret_cast<void*>(thinkDetour),
                                            reinterpret_cast<void**>(&g_originalThink));
    return thinkHooked;
}

void offer(uint8_t slot, uint16_t seq, const Decision& decision) {
    if (slot < game::kEnemyPoolSlots) pendingFor(slot).offer(seq, decision);
}

void seed(uint8_t slot, const Decision& decision) {
    if (slot < game::kEnemyPoolSlots) pendingFor(slot).seed(decision);
}

void reset() {
    g_pending.fill({});
    g_pendingRoom = scene::current();
}

void onHitReplayed(uint8_t slot) {
    if (slot < game::kEnemyPoolSlots) pendingFor(slot).supersede();
}

}  // namespace enemy_think
