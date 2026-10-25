#include "enemy_drift.h"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <vector>

#include "enemy_drift_rule.h"
#include "enemy_observer_game.h"
#include "enemy_observer_wire.h"
#include "enemy_policy.h"
#include "enemy_registry.h"
#include "game.h"
#include "game_tick.h"
#include "log.h"
#include "pose_wire.h"

namespace {

namespace rule = enemy_drift_rule;

constexpr uint32_t kDrivenFreshMs = 200;  // an enemy last compared longer ago is not shown from the stream right now
constexpr size_t kLineSize = 224;

// What the trace keeps about the enemy in one pool slot; it starts over when the slot holds another enemy.
struct Watch {
    uintptr_t enemy = 0;
    uint32_t comparedMs = 0;
    rule::SlotTrackers trackers;
};

std::array<Watch, game::kEnemyPoolSlots> g_watch;
rule::LineBudget g_budget;
rule::Summary g_summary;
rule::ShotLedger g_ledger;
std::vector<rule::Shot> g_closed;  // reused by the tick

// One budgeted line; a line over the budget is counted for the summary instead.
void line(uint32_t nowMs, const char* format, ...) {
    if (!g_budget.allow(nowMs)) {
        ++g_summary.suppressed;
        return;
    }
    char text[kLineSize];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    logger::write("%s", text);
}

void report(rule::Event event, rule::Field field, size_t index, uint8_t classIndex, const rule::Values& expected,
            const rule::Values& actual, uint32_t driftMs, uint32_t nowMs) {
    const char* const cls = enemy_policy::nameOf(classIndex);
    if (event == rule::Event::Cleared) {
        line(nowMs, "enemy_drift: slot %zu (%s) %s matches the simulator again after %u ms", index, cls, rule::name(field), driftMs);
        return;
    }
    ++g_summary.onsets[static_cast<size_t>(field)];
    line(nowMs, "enemy_drift: slot %zu (%s) %s here %d sim %d for %u ms%s", index, cls, rule::name(field), actual.of(field),
         expected.of(field), driftMs, rule::corrected(field) ? ", corrected" : "");
}

// The first living, freshly compared enemy whose shot-skip bits are set right now, and how many there are.
rule::SkipSighting skippingNow(uint32_t nowMs) {
    rule::SkipSighting sighting;
    for (size_t index = 0; index < g_watch.size(); ++index) {
        const uintptr_t enemy = enemy_registry::enemyAt(static_cast<int>(index));
        const Watch& watch = g_watch[index];
        enemy_observer_wire::PoseEntry now{};
        if (!enemy || watch.enemy != enemy || nowMs - watch.comparedMs > kDrivenFreshMs || !enemy_observer_game::read(enemy, now)) continue;
        if (now.hp <= 0 || (now.flags6a0c & enemy_observer_rule::kShotSkipBits) == 0) continue;
        if (sighting.count++ == 0) {
            sighting.slot = static_cast<uint8_t>(index);
            sighting.classIndex = static_cast<uint8_t>(enemy_policy::classIndexOf(game::readPointer(enemy)));
            sighting.flags6a0c = now.flags6a0c;
        }
    }
    return sighting;
}

void reportShot(const rule::Shot& shot, uint32_t nowMs) {
    ++g_summary.shotAnomalies;
    if (shot.skipping.count != 0) {
        line(nowMs, "enemy_drift: shot with %u living observed enemies skipping shots, first slot %u (%s) flags 0x%x; hits routed %u applied %u refused %u",
             shot.skipping.count, shot.skipping.slot, enemy_policy::nameOf(shot.skipping.classIndex), shot.skipping.flags6a0c,
             shot.routed, shot.applied, shot.refused);
        return;
    }
    line(nowMs, "enemy_drift: shot hits routed %u applied %u refused %u after %u ms", shot.routed, shot.applied, shot.refused,
         rule::kAnswerWindowMs);
}

void reportSummary() {
    const rule::Summary summary = g_summary.take();
    const auto onsets = [&](rule::Field field) { return summary.onsets[static_cast<size_t>(field)]; };
    logger::write("enemy_drift: %u s summary: onsets hp %u alive %u skip %u record %u motion %u, corrections %u, shots %u (%u anomalous), %u lines suppressed",
                  rule::kSummaryMs / 1000, onsets(rule::Field::Hp), onsets(rule::Field::Alive), onsets(rule::Field::ShotSkip),
                  onsets(rule::Field::RecordState), onsets(rule::Field::Motion), summary.corrections, summary.shotsChecked,
                  summary.shotAnomalies, summary.suppressed);
}

void onTick() {
    const uint32_t nowMs = pose_wire::clockMs();
    g_closed.clear();
    g_ledger.expire(nowMs, g_closed);
    for (const rule::Shot& shot : g_closed) {
        ++g_summary.shotsChecked;
        if (shot.anomalous()) reportShot(shot, nowMs);
    }
    if (g_summary.due(nowMs)) reportSummary();
}

}  // namespace

namespace enemy_drift {

bool enable() {
    game_tick::addCallback("enemy_drift", onTick);
    return true;
}

void afterMove(size_t index, uintptr_t enemy, uint8_t classIndex, const enemy_observer_rule::Sample& written) {
    enemy_observer_wire::PoseEntry read{};
    if (index >= g_watch.size() || !enemy_observer_game::read(enemy, read)) return;
    Watch& watch = g_watch[index];
    if (watch.enemy != enemy) watch = Watch{enemy, 0, {}};
    const uint32_t nowMs = pose_wire::clockMs();
    watch.comparedMs = nowMs;
    const rule::Values expected = rule::expectedOf(written);
    const rule::Values actual = rule::actualOf(read);
    bool needsCorrection = false;
    for (size_t field = 0; field < rule::kFieldCount; ++field) {
        const auto which = static_cast<rule::Field>(field);
        if (!expected.compares(which)) continue;
        needsCorrection |= rule::corrected(which) && expected.of(which) != actual.of(which);
        const rule::Event event = watch.trackers.observe(which, expected.of(which), actual.of(which), nowMs);
        if (event != rule::Event::None) {
            report(event, which, index, classIndex, expected, actual, watch.trackers.driftMs(which, nowMs), nowMs);
        }
    }
    if (!needsCorrection) return;
    enemy_observer_game::reassert(enemy, written);
    ++g_summary.corrections;
}

void shotFired() {
    if (!game_tick::onGameThread()) return;
    const uint32_t nowMs = pose_wire::clockMs();
    g_ledger.fired(nowMs, skippingNow(nowMs));
}

void hitRouted(bool answeredHere) { g_ledger.routed(pose_wire::clockMs(), answeredHere); }

void hitRefused() { g_ledger.refused(pose_wire::clockMs()); }

void hitAnswered() { g_ledger.applied(); }

}  // namespace enemy_drift
