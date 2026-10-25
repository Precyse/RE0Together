// Checks the pure rules of the boss hand-over (no game): tokens, members against what a machine has, the transaction's
// stages for the old and the new authority, its timeouts and the single-machine commit. Exit 0 when every check passes.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "../src/boss_handover_rule.h"
#include "../src/door_leave_rule.h"

namespace {

namespace rule = boss_handover_rule;
namespace wire = boss_wire;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

constexpr uint32_t kToken = (1u << 24) | 7;
constexpr uint32_t kOtherToken = (2u << 24) | 8;
constexpr uint32_t kStartMs = 1000;
constexpr uint16_t kSpawn = 0x31;
constexpr int16_t kHp = 600;

uint8_t bossClassIndex() { return static_cast<uint8_t>(enemy_policy::classIndexOf(game::kBossVtables[0])); }

wire::Member member(uint8_t slot, uint16_t spawn = kSpawn, int16_t hp = kHp) {
    wire::Member m{};
    m.slot = slot;
    m.classIndex = bossClassIndex();
    m.spawnId = spawn;
    m.hp = hp;
    return m;
}

rule::Local local(uint16_t spawn = kSpawn, int16_t hp = kHp) { return {true, bossClassIndex(), spawn, hp}; }

void testTokens() {
    check(rule::nextToken(0, 0) == ((1u << 24) | 1), "the first token of slot 0");
    check(rule::nextToken(0, 1) == ((2u << 24) | 1), "the first token of slot 1");
    check(rule::nextToken((1u << 24) | 5, 0) == ((1u << 24) | 6), "the counter counts up");
    check(rule::nextToken((1u << 24) | rule::kTokenCounterMask, 0) == (1u << 24), "the counter wraps inside its bits");
    check(rule::nextToken(0, 0) != rule::nextToken(0, 1), "two slots never issue the same token");
    check(rule::nextToken(0, 7) != 0, "a token is never 0");
}

void testFit() {
    const wire::Member m = member(3);
    check(rule::fitOf(m, local()) == rule::Fit::Ok, "the same living enemy fits");
    check(rule::fitOf(m, {}) == rule::Fit::Missing, "an empty slot is missing");
    check(rule::fitOf(m, local(0x32)) == rule::Fit::Mismatch, "another spawn in the slot is a mismatch");
    rule::Local otherClass = local();
    otherClass.classIndex = static_cast<uint8_t>(otherClass.classIndex + 1);
    check(rule::fitOf(m, otherClass) == rule::Fit::Mismatch, "another class in the slot is a mismatch");
    check(rule::fitOf(m, local(kSpawn, -1)) == rule::Fit::Dead, "a dead enemy is dead");
    check(rule::fitOf(m, local(kSpawn, 0)) == rule::Fit::Dead, "zero HP is dead");
}

void testPrepareAcceptance() {
    const std::vector<wire::Member> members = {member(3), member(5, 0x40)};
    check(rule::acceptsPrepare(members, std::vector<rule::Local>{local(), local(0x40)}), "every member present is accepted");
    check(rule::acceptsPrepare(members, std::vector<rule::Local>{local(), local(0x40, -1)}), "a member already dead here is accepted");
    check(!rule::acceptsPrepare(members, std::vector<rule::Local>{local(), {}}), "a missing member is refused");
    check(!rule::acceptsPrepare(members, std::vector<rule::Local>{local(), local(0x41)}), "a mismatching member is refused");
    rule::Local otherRecord = local(0x40);
    otherRecord.spawnRecord = 9;
    check(!rule::acceptsPrepare(members, std::vector<rule::Local>{local(), otherRecord}), "the same spawn id from another record is refused");
    check(!rule::acceptsPrepare(members, std::vector<rule::Local>{local()}), "a locals list of another length is refused");
}

void testAliveMask() {
    const std::vector<wire::Member> members = {member(3), member(5, 0x40), member(8, 0x41)};
    check(rule::aliveMask(members, std::vector<rule::Local>{local(), local(0x40), local(0x41)}) == 0x7, "all alive");
    check(rule::aliveMask(members, std::vector<rule::Local>{local(), local(0x40, -1), local(0x41)}) == 0x5, "the dead one leaves its bit clear");
    check(rule::aliveMask(members, std::vector<rule::Local>{local(), {}, local(0x99)}) == 0x1, "missing and other enemies are not alive");
    check(rule::aliveMask(members, std::vector<rule::Local>{}) == 0, "no locals, none alive");
}

void testFlipPlan() {
    const std::vector<wire::Member> members = {member(3), member(5, 0x40), member(8, 0x41)};
    rule::Plan plan = rule::planFlip(members, std::vector<rule::Local>{local(), local(0x40), local(0x41)});
    check(plan.verdict == rule::Verdict::Apply && plan.resume == 0x7 && plan.alive == 0x7, "every living member is resumed");
    plan = rule::planFlip(members, std::vector<rule::Local>{local(), local(0x40, -1), local(0x41)});
    check(plan.verdict == rule::Verdict::Apply && plan.resume == 0x5 && plan.alive == 0x5, "a member that died meanwhile is left dead");
    plan = rule::planFlip(members, std::vector<rule::Local>{local(), {}, local(0x41)});
    check(plan.verdict == rule::Verdict::Frozen && plan.badSlot == 5 && plan.alive == 0x5, "a missing member freezes the hand-over");
    check(plan.resume == 0x5, "only the members that fit are held (the missing slot's bystander is not)");
    plan = rule::planFlip(members, std::vector<rule::Local>{local(), local(0x40), local(0x77)});
    check(plan.verdict == rule::Verdict::Frozen && plan.badSlot == 8, "another enemy in a member's slot freezes it");
    check(plan.resume == 0x3, "the slot holding another enemy is not held");
    plan = rule::planFlip(members, std::vector<rule::Local>{{}, {}, {}});
    check(plan.verdict == rule::Verdict::Frozen && plan.badSlot == 3, "the first offending slot is named");
    check(rule::resumeHp(600, 600) == 600, "HP unchanged");
    check(rule::resumeHp(600, 450) == 450, "damage dealt while frozen is kept");
    check(rule::resumeHp(300, 600) == 300, "the snapshot's HP never goes up");
    check(rule::resumeHp(0, 0) == 1, "a resumed member always has HP");
}

void testDeparture() {
    door_leave_rule::Departure departure;
    check(!departure.starts(false, true, true), "no door, no departure");
    check(!departure.starts(true, false, true), "a door out of a room this machine does not run");
    check(!departure.starts(true, true, false), "a door with the other player elsewhere");
    check(departure.starts(true, true, true), "the first tick of the door out of a shared, run room");
    check(!departure.starts(true, true, true), "the door is reported once");
    check(!departure.starts(false, true, true), "the door ends");
    check(departure.starts(true, true, true), "the next door is reported again");
}

void testHappyPath() {
    rule::Machine oldAuthority;
    rule::Machine newAuthority;
    check(!oldAuthority.active() && !oldAuthority.inFlight(), "a new machine is idle");

    oldAuthority.startPrepare(kToken, 2, 0x3, kStartMs);
    check(oldAuthority.stage() == rule::Stage::Preparing && oldAuthority.role() == rule::Role::Old && oldAuthority.inFlight(), "prepare sent");
    newAuthority.acceptPrepare(kToken, 2, 0x3, kStartMs + 20);
    check(newAuthority.stage() == rule::Stage::Prepared && newAuthority.role() == rule::Role::New && newAuthority.owns(kToken), "prepare accepted");

    check(!oldAuthority.onReady(kOtherToken, kStartMs + 40), "a ready for another token is ignored");
    check(oldAuthority.onReady(kToken, kStartMs + 40) && oldAuthority.stage() == rule::Stage::Committed, "ready commits");
    check(!oldAuthority.onReady(kToken, kStartMs + 41), "a second ready is ignored");
    check(!newAuthority.onCommit(kOtherToken, kStartMs + 60), "a commit for another token is ignored");
    check(newAuthority.onCommit(kToken, kStartMs + 60) && newAuthority.stage() == rule::Stage::Committed, "commit heard");

    oldAuthority.leave(kStartMs + 3000);
    check(oldAuthority.stage() == rule::Stage::Away && oldAuthority.active() && !oldAuthority.inFlight(), "the old authority left the room");

    newAuthority.run(kToken, 2, 0x3, kStartMs + 3100);
    check(newAuthority.stage() == rule::Stage::Running && newAuthority.count() == 2 && newAuthority.alive() == 0x3, "the snapshot is applied");
    check(!newAuthority.noteAlive(0x3), "an unchanged alive mask is not news");
    check(newAuthority.noteAlive(0x1) && newAuthority.alive() == 0x1, "a member died");
    check(oldAuthority.noteAlive(0x1) && oldAuthority.alive() == 0x1, "the old authority hears it");
    newAuthority.finish();
    oldAuthority.finish();
    check(!newAuthority.active() && !oldAuthority.active(), "both finish");
}

void testCancel() {
    rule::Machine oldAuthority;
    oldAuthority.startPrepare(kToken, 1, 0x1, kStartMs);
    check(!oldAuthority.onCancel(kOtherToken), "a cancel for another token is ignored");
    check(oldAuthority.onCancel(kToken) && !oldAuthority.active(), "a cancel before the commit ends the old authority's prepare");

    rule::Machine newAuthority;
    newAuthority.acceptPrepare(kToken, 1, 0x1, kStartMs);
    check(newAuthority.onCancel(kToken) && !newAuthority.active(), "a cancel drops an accepted prepare");
    newAuthority.acceptPrepare(kToken, 1, 0x1, kStartMs);
    newAuthority.onCommit(kToken, kStartMs + 10);
    check(newAuthority.onCancel(kToken) && !newAuthority.active(), "a cancel drops a committed one");

    newAuthority.run(kToken, 1, 0x1, kStartMs);
    check(!newAuthority.onCancel(kToken) && newAuthority.stage() == rule::Stage::Running, "a running hand-over is past cancelling");

    rule::Machine committed;
    committed.startPrepare(kToken, 1, 0x1, kStartMs);
    committed.onReady(kToken, kStartMs + 10);
    check(!committed.onCancel(kToken), "the old authority's committed stage is cancelled by its own lapse, not by a message");
}

void testTimeouts() {
    rule::Machine oldAuthority;
    check(oldAuthority.lapse(kStartMs + 99999, true) == rule::Lapse::None, "an idle machine never lapses");
    oldAuthority.startPrepare(kToken, 1, 0x1, kStartMs);
    check(oldAuthority.lapse(kStartMs + rule::kPrepareTimeoutMs - 1, true) == rule::Lapse::None, "a prepare waits for its ready");
    check(oldAuthority.lapse(kStartMs + rule::kPrepareTimeoutMs, true) == rule::Lapse::CancelPrepare, "a prepare lapses after 5 s");
    check(oldAuthority.lapse(kStartMs + 10, false) == rule::Lapse::CancelPrepare, "a prepare is cancelled when the peer left the room");

    oldAuthority.onReady(kToken, kStartMs + 100);
    check(oldAuthority.lapse(kStartMs + 100 + rule::kCommitWaitMs - 1, true) == rule::Lapse::None, "a commit waits for the room to change hands");
    check(oldAuthority.lapse(kStartMs + 100 + rule::kCommitWaitMs, true) == rule::Lapse::CancelCommit, "a commit lapses");
    check(oldAuthority.lapse(kStartMs + 200, false) == rule::Lapse::CancelCommit, "a commit is cancelled when the peer left the room");
    oldAuthority.leave(kStartMs + 300);
    check(oldAuthority.lapse(kStartMs + 99999, false) == rule::Lapse::None, "an old authority that left the room waits for the outcome");

    rule::Machine newAuthority;
    newAuthority.acceptPrepare(kToken, 1, 0x1, kStartMs);
    check(newAuthority.lapse(kStartMs + rule::kPrepareTimeoutMs - 1, false) == rule::Lapse::None, "an accepted prepare waits for the commit");
    check(newAuthority.lapse(kStartMs + rule::kPrepareTimeoutMs, false) == rule::Lapse::DropPrepared, "an accepted prepare lapses");
    newAuthority.onCommit(kToken, kStartMs + 100);
    check(newAuthority.lapse(kStartMs + 100 + rule::kCommitWaitMs, false) == rule::Lapse::DropCommit, "a heard commit lapses if the room never changes hands");
    newAuthority.run(kToken, 1, 0x1, kStartMs + 200);
    check(newAuthority.lapse(kStartMs + 99999, false) == rule::Lapse::None, "a running hand-over never lapses");

    rule::Machine wrapped;
    constexpr uint32_t kNearWrap = 0xfffffff0u;
    wrapped.startPrepare(kToken, 1, 0x1, kNearWrap);
    check(wrapped.lapse(kNearWrap + 100, true) == rule::Lapse::None && wrapped.lapse(kNearWrap + rule::kPrepareTimeoutMs, true) == rule::Lapse::CancelPrepare,
          "the elapsed time survives the clock wrapping");
}

void testSingleMachineCommit() {
    rule::Machine machine;
    machine.run(kOtherToken, 2, 0x3, kStartMs);
    check(machine.stage() == rule::Stage::Running && machine.role() == rule::Role::New && machine.token() == kOtherToken,
          "a machine that takes a room over alone starts running at once");

    rule::Machine prepared;
    prepared.acceptPrepare(kToken, 2, 0x3, kStartMs);
    prepared.run(kToken, 2, 0x1, kStartMs + 500);
    check(prepared.stage() == rule::Stage::Running && prepared.token() == kToken && prepared.alive() == 0x1,
          "a prepare whose commit never came is applied when the room changes hands");
}

void testFrozen() {
    rule::Machine machine;
    machine.acceptPrepare(kToken, 2, 0x3, kStartMs);
    machine.onCommit(kToken, kStartMs + 10);
    machine.freeze(kStartMs + 20);
    check(machine.stage() == rule::Stage::Frozen && machine.active() && !machine.inFlight(), "a commit that cannot finish is frozen");
    check(machine.lapse(kStartMs + 20 + rule::kFrozenReleaseMs - 1, false) == rule::Lapse::None, "a frozen hand-over waits for the resync");
    check(machine.lapse(kStartMs + 20 + rule::kFrozenReleaseMs, false) == rule::Lapse::ReleaseFrozen,
          "a frozen hand-over is released after the wait, whether or not the peer is there");
    check(machine.lapse(kStartMs + 20 + rule::kFrozenReleaseMs, true) == rule::Lapse::ReleaseFrozen, "the peer's presence does not matter");
    check(!machine.onCancel(kToken), "a cancel does not unfreeze it");
    rule::Machine released = machine;
    released.run(kToken, 2, 0x1, kStartMs + 20 + rule::kFrozenReleaseMs);
    check(released.stage() == rule::Stage::Running && released.alive() == 0x1 && released.token() == kToken,
          "the release carries on as a running hand-over with the members that fit");
    check(released.lapse(kStartMs + 99999, false) == rule::Lapse::None, "a released hand-over never lapses again");
    machine.finish();
    check(!machine.active(), "it ends with the room");

    rule::Machine idle;
    idle.freeze(kStartMs);
    check(!idle.active(), "an idle machine cannot freeze");
    rule::Machine old;
    old.startPrepare(kToken, 1, 0x1, kStartMs);
    old.freeze(kStartMs);
    check(old.stage() == rule::Stage::Preparing, "only the new authority freezes");
}

}  // namespace

int main() {
    testTokens();
    testFit();
    testPrepareAcceptance();
    testAliveMask();
    testFlipPlan();
    testDeparture();
    testHappyPath();
    testCancel();
    testTimeouts();
    testSingleMachineCommit();
    testFrozen();
    if (g_failures == 0) std::printf("boss_handover_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
