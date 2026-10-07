#include "enemy_decision.h"

#include <algorithm>
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

using NoArgumentFunction = void(__fastcall*)(void* enemy, void* edx);
using SetActionFunction = void(__fastcall*)(void* enemy, void* edx, int32_t state, int32_t id, int32_t a, int32_t b);
NoArgumentFunction g_originalThink = nullptr;
NoArgumentFunction g_originalCommit = nullptr;
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

bool readAction(uintptr_t enemy, uintptr_t offset, Action& out) { return game::readMemory(enemy + offset, out.word); }

bool boundaryClass(uintptr_t enemy) {
    const uintptr_t vtable = game::readPointer(enemy);
    return std::find(game::kEnemyBoundaryVtables.begin(), game::kEnemyBoundaryVtables.end(), vtable) !=
           game::kEnemyBoundaryVtables.end();
}

// Every class this module drives stores its record through the plain setAction (game.h), so the original store is
// the class's own call.
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
    logger::write("enemy_decision: slot %u realigned at a decision, drift %.1f", slot, drift);
}

void apply(void* enemy, uint8_t slot, const Decision& decision, const Action& action) {
    realign(reinterpret_cast<uintptr_t>(enemy), slot, decision);
    setAction(enemy, action);
    debug_stats::count(debug_stats::Counter::EnemyDecisionsApplied);
    const auto now = Clock::now();
    if (now - g_lastApplyLog < kLogInterval) return;
    g_lastApplyLog = now;
    logger::write("enemy_decision: slot %u took the owner's decision (%d,%d,%d,%d)", slot, action.word[0],
                  action.word[1], action.word[2], action.word[3]);
}

// The owner's newest decision for `slot`, if it was made by an enemy of the same class.
bool takeDecision(uintptr_t enemy, uint8_t slot, Decision& out) {
    if (!pendingFor(slot).take(out)) return false;
    if (out.vtable == game::readPointer(enemy)) return true;
    logger::write("enemy_decision: slot %u decision dropped, owner class 0x%x", slot, out.vtable);
    return false;
}

// The owner reports a record its enemy chose, with the pose it chose it from.
void report(uintptr_t enemy, int slot, const Action& action) {
    Decision decision;
    decision.vtable = static_cast<uint32_t>(game::readPointer(enemy));
    decision.action = action;
    if (!game::readTransform(enemy, decision.pos, decision.quat)) return;
    enemy_net::sendDecision(static_cast<uint8_t>(slot), decision);
}

// The follower's think step: it runs, its own choices are dropped, and the owner's newest decision is set instead.
void thinkForOwner(void* enemy, void* edx, uint8_t slot) {
    const uintptr_t self = reinterpret_cast<uintptr_t>(enemy);
    g_dropSetActionOf = self;
    g_originalThink(enemy, edx);
    g_dropSetActionOf = 0;
    Decision decision;
    if (takeDecision(self, slot, decision)) apply(enemy, slot, decision, decision.action);
}

void __fastcall thinkDetour(void* enemy, void* edx) {
    const uintptr_t self = reinterpret_cast<uintptr_t>(enemy);
    const int slot = enemy_registry::slotOf(self);
    if (slot == enemy_registry::kNoSlot) return g_originalThink(enemy, edx);
    if (enemy_state::followsOwner()) return thinkForOwner(enemy, edx, static_cast<uint8_t>(slot));
    Action before;
    const bool reports = enemy_state::leadsPeer() && readAction(self, game::kEnemyActionOffset, before);
    g_originalThink(enemy, edx);
    Action after;
    if (reports && readAction(self, game::kEnemyActionOffset, after) && after != before) report(self, slot, after);
}

void __fastcall setActionDetour(void* enemy, void* edx, int32_t state, int32_t id, int32_t a, int32_t b) {
    if (reinterpret_cast<uintptr_t>(enemy) == g_dropSetActionOf) return;
    g_originalSetAction(enemy, edx, state, id, a, b);
}

// The follower's own action boundary: the owner's newest action of the same state replaces the one its AI chose.
void boundaryForOwner(void* enemy, uint8_t slot, const Action& local) {
    Decision decision;
    if (!takeDecision(reinterpret_cast<uintptr_t>(enemy), slot, decision)) return;
    if (enemy_follow_rule::atOwnBoundary(local, decision.action) != enemy_follow_rule::AtBoundary::Apply) return;
    apply(enemy, slot, decision, enemy_follow_rule::startOf(decision.action));
}

// The executor's commit: for a boundary class, a record that differs from the previous frame's is an action boundary.
// The commit runs first (the previous record becomes this frame's choice); a replacement set after it reads as a new
// action on the next frame.
void __fastcall commitDetour(void* enemy, void* edx) {
    const uintptr_t self = reinterpret_cast<uintptr_t>(enemy);
    Action record;
    Action previous;
    const bool boundary = boundaryClass(self) && readAction(self, game::kEnemyActionOffset, record) &&
                          readAction(self, game::kEnemyPreviousActionOffset, previous) &&
                          enemy_follow_rule::atBoundary(record, previous);
    g_originalCommit(enemy, edx);
    const int slot = boundary ? enemy_registry::slotOf(self) : enemy_registry::kNoSlot;
    if (slot == enemy_registry::kNoSlot) return;
    if (enemy_state::followsOwner()) return boundaryForOwner(enemy, static_cast<uint8_t>(slot), record);
    if (enemy_state::leadsPeer()) report(self, slot, record);
}

}  // namespace

namespace enemy_decision {

bool install() {
    const bool setActionHooked =
        hooks::install("enemy setAction", game::kEnemyBaseSetActionFunction, reinterpret_cast<void*>(setActionDetour),
                       reinterpret_cast<void**>(&g_originalSetAction));
    const bool thinkHooked = setActionHooked &&
                             hooks::install("enemy think", game::kEnemyThinkFunction, reinterpret_cast<void*>(thinkDetour),
                                            reinterpret_cast<void**>(&g_originalThink));
    const bool commitHooked =
        setActionHooked && hooks::install("enemy action commit", game::kEnemyActionCommitFunction,
                                          reinterpret_cast<void*>(commitDetour),
                                          reinterpret_cast<void**>(&g_originalCommit));
    return thinkHooked && commitHooked;
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

}  // namespace enemy_decision
