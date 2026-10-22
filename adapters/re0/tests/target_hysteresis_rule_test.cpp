// Checks the boss target hysteresis rule (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/target_hysteresis_rule.h"

namespace {

namespace rule = target_hysteresis_rule;

constexpr uint8_t kBilly = 0;
constexpr uint8_t kRebecca = 1;
constexpr int64_t kStart = 100000;
const uint8_t kIds[rule::kCharacterCount] = {kBilly, kRebecca};

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

// Billy is the controlled character, Rebecca the partner.
uint8_t pick(const rule::Tuning& tuning, rule::Memory& memory, uint8_t native, float billy, float rebecca, int64_t now) {
    const float distance[rule::kCharacterCount] = {billy, rebecca};
    return rule::choose(tuning, memory, native, distance, kIds, now);
}

constexpr rule::Tuning kTuning = rule::kLargeBossTuning;
constexpr int64_t kJustAfterLock = kStart + kTuning.lockMs + 1;

void tuningConstants() {
    check(rule::kLargeBossTuning.bonus == 3000.0f && rule::kLargeBossTuning.attackerMs == 6000 &&
              rule::kLargeBossTuning.lockMs == 3000 && rule::kLargeBossTuning.margin == 1500.0f,
          "large boss: bonus 3000, window 6000 ms, lock 3000 ms, margin 1500");
    check(rule::kStalkerTuning.bonus == 1500.0f && rule::kStalkerTuning.attackerMs == 4000 &&
              rule::kStalkerTuning.lockMs == 2000 && rule::kStalkerTuning.margin == 1000.0f,
          "stalker: bonus 1500, window 4000 ms, lock 2000 ms, margin 1000");
}

void acquisition() {
    rule::Memory memory;
    check(pick(kTuning, memory, kBilly, 800, 5000, kStart) == kBilly, "first acquisition takes native");
    check(memory.reason == rule::Reason::Acquired, "first acquisition: reason");
    check(memory.lockUntil == kStart + kTuning.lockMs, "first acquisition starts the lock");

    rule::Memory none;
    check(pick(kTuning, none, rule::kNone, rule::kIneligible, rule::kIneligible, kStart) == rule::kNone,
          "native none keeps none");
    check(none.lockUntil == 0, "native none does not start a lock");

    rule::Memory ineligibleNative;
    check(pick(kTuning, ineligibleNative, kRebecca, 800, rule::kIneligible, kStart) == rule::kNone,
          "an ineligible native counts as none");
}

void lockAndMargin() {
    rule::Memory memory;
    pick(kTuning, memory, kBilly, 800, 5000, kStart);
    check(pick(kTuning, memory, kRebecca, 5000, 100, kStart + 1000) == kBilly, "native flip inside the lock is refused");
    check(pick(kTuning, memory, kRebecca, 2000, 1000, kJustAfterLock) == kBilly,
          "after the lock, a flip that is nearer by less than the margin is refused");
    check(pick(kTuning, memory, kRebecca, 2000, 501, kJustAfterLock) == kBilly,
          "after the lock, just under the margin is refused");
    check(pick(kTuning, memory, kRebecca, 2000, 500, kJustAfterLock) == kRebecca,
          "after the lock, a flip nearer by exactly the margin is accepted");
    check(memory.reason == rule::Reason::Nearer, "flip reason: nearer");
    check(memory.lockUntil == kJustAfterLock + kTuning.lockMs, "a switch starts a new lock");
    check(pick(kTuning, memory, kBilly, 100, 5000, kJustAfterLock + 1) == kRebecca, "the new lock holds too");
    check(pick(kTuning, memory, kBilly, 100, 5000, kJustAfterLock + kTuning.lockMs) == kBilly,
          "and releases after lockMs");
}

void attackerPull() {
    rule::Memory memory;
    pick(kTuning, memory, kBilly, 800, 3000, kStart);
    rule::noteAttacker(kTuning, memory, kRebecca, kStart + 100);
    check(pick(kTuning, memory, kBilly, 800, 3000, kStart + 200) == kBilly, "attacker pull waits for the lock");
    // Rebecca at 3000 - 3000 bonus = 0 against Billy at 800: nearer by 800, under the margin of 1500.
    check(pick(kTuning, memory, kBilly, 800, 3000, kJustAfterLock) == kBilly, "attacker bonus alone under the margin");
    check(pick(kTuning, memory, kBilly, 1600, 3000, kJustAfterLock) == kRebecca,
          "attacker pull within the window (3000 - 3000 + 1500 <= 1600)");
    check(memory.reason == rule::Reason::Attacker, "pull reason: attacker");

    rule::Memory expired;
    pick(kTuning, expired, kBilly, 800, 3000, kStart);
    rule::noteAttacker(kTuning, expired, kRebecca, kStart + 100);
    const int64_t afterWindow = kStart + 100 + kTuning.attackerMs;
    check(pick(kTuning, expired, kBilly, 1600, 3000, afterWindow) == kBilly, "no pull after the window");

    rule::Memory notAttacker;
    pick(kTuning, notAttacker, kBilly, 800, 3000, kStart);
    rule::noteAttacker(kTuning, notAttacker, rule::kNone, kStart + 100);
    check(pick(kTuning, notAttacker, kBilly, 1600, 3000, kJustAfterLock) == kBilly, "no attacker, no pull");
}

void attackerKeepsTarget() {
    rule::Memory memory;
    pick(kTuning, memory, kBilly, 800, 3000, kStart);
    rule::noteAttacker(kTuning, memory, kBilly, kStart + 100);
    // Rebecca is nearer by 2000 (over the margin), but Billy's recent attack takes 3000 off his distance.
    check(pick(kTuning, memory, kRebecca, 4000, 2000, kJustAfterLock) == kBilly,
          "the attacker's bonus keeps it against a nearer native");
    check(pick(kTuning, memory, kRebecca, 4000, 1000, kJustAfterLock + kTuning.attackerMs) == kRebecca,
          "once the window is over the bonus is gone");
}

void ineligibleTarget() {
    rule::Memory memory;
    pick(kTuning, memory, kBilly, 800, 5000, kStart);
    check(pick(kTuning, memory, kRebecca, rule::kIneligible, 5000, kStart + 10) == kRebecca,
          "a current target that stops being eligible switches at once, inside the lock");
    check(memory.reason == rule::Reason::Ineligible, "reason: target ineligible");
    check(memory.lockUntil == kStart + 10 + kTuning.lockMs, "and starts a new lock");
    check(pick(kTuning, memory, rule::kNone, rule::kIneligible, rule::kIneligible, kStart + 20) == rule::kNone,
          "nobody eligible: none");
}

}  // namespace

int main() {
    tuningConstants();
    acquisition();
    lockAndMargin();
    attackerPull();
    attackerKeepsTarget();
    ineligibleTarget();
    if (g_failures == 0) std::printf("target_hysteresis_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
