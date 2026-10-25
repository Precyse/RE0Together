// Checks the rescue chord's pure rules (no game): the hold that makes a press, the ladder of a scene (release, reload,
// toast), each reload refusal, the second reload in a scene, the orphan lock's four seconds and its resets, and what an
// event release may pop. Exit 0 when every check passes.
#include <cstdio>
#include <cstring>

#include "../src/rescue_rule.h"

namespace {

namespace rule = rescue_rule;
using rule::Action;
using rule::Refusal;

constexpr uint16_t kSceneA = 0x2d;
constexpr uint16_t kSceneB = 0x2e;
constexpr uint32_t kStartMs = 100000;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

rule::Conditions allowed() {
    rule::Conditions conditions;
    conditions.hasDoor = true;
    return conditions;
}

void testChord() {
    rule::Chord chord;
    check(!chord.update(kStartMs, true), "pressing the chord is not a press yet");
    check(!chord.update(kStartMs + rule::kHoldMs - 1, true), "just short of the hold is not a press");
    check(chord.update(kStartMs + rule::kHoldMs, true), "held for kHoldMs is one press");
    check(!chord.update(kStartMs + rule::kHoldMs + 1, true), "the same hold does not press again");
    check(!chord.update(kStartMs + 10 * rule::kHoldMs, true), "however long it is held");
    check(!chord.update(kStartMs + 10 * rule::kHoldMs + 1, false), "letting go is not a press");
    check(!chord.update(kStartMs + 10 * rule::kHoldMs + 2, true), "holding again starts the count again");
    check(chord.update(kStartMs + 11 * rule::kHoldMs + 2, true), "and presses again after kHoldMs");

    rule::Chord interrupted;
    interrupted.update(kStartMs, true);
    interrupted.update(kStartMs + rule::kHoldMs - 10, true);
    interrupted.update(kStartMs + rule::kHoldMs - 5, false);
    check(!interrupted.update(kStartMs + rule::kHoldMs, true), "a hold broken near its end starts over");
    check(interrupted.update(kStartMs + 2 * rule::kHoldMs, true), "and presses kHoldMs after the new start");

    rule::Chord wrapped;
    const uint32_t nearWrap = 0xffffffffu - 500;
    wrapped.update(nearWrap, true);
    check(wrapped.update(nearWrap + rule::kHoldMs, true), "the hold survives the millisecond clock wrapping");
}

void testLadderOrder() {
    rule::Ladder ladder;
    check(ladder.press(kStartMs, kSceneA, allowed()).action == Action::Release, "the first press releases");
    const rule::Decision second = ladder.press(kStartMs + 1000, kSceneA, allowed());
    check(second.action == Action::Reload && second.refusal == Refusal::None, "the second reloads the room");
    const rule::Decision third = ladder.press(kStartMs + 2000, kSceneA, allowed());
    check(third.action == Action::Toast && third.refusal == Refusal::None, "the third only toasts");
    check(ladder.press(kStartMs + 3000, kSceneA, allowed()).action == Action::Toast, "and so does every later one");
}

void testLadderResets() {
    rule::Ladder byScene;
    byScene.press(kStartMs, kSceneA, allowed());
    check(byScene.press(kStartMs + 1000, kSceneB, allowed()).action == Action::Release, "a press in another scene releases first");
    check(byScene.press(kStartMs + 2000, kSceneB, allowed()).action == Action::Reload, "then reloads that scene");

    rule::Ladder onTick;
    onTick.press(kStartMs, kSceneA, allowed());
    onTick.onScene(kSceneB);
    check(onTick.press(kStartMs + 1000, kSceneB, allowed()).action == Action::Release, "a scene change seen by the tick resets the ladder");

    rule::Ladder byTime;
    byTime.press(kStartMs, kSceneA, allowed());
    check(byTime.press(kStartMs + rule::kLadderResetMs - 1, kSceneA, allowed()).action == Action::Reload, "pressed again within the reset time it climbs");
    check(byTime.press(kStartMs + 2 * rule::kLadderResetMs - 1, kSceneA, allowed()).action == Action::Release,
          "after kLadderResetMs since the last press it starts over");

    rule::Ladder loading;
    loading.press(kStartMs, kSceneA, allowed());
    loading.onScene(rule::kNoScene);
    check(loading.press(kStartMs + 1000, kSceneA, allowed()).action == Action::Reload, "no scene while a room loads is not a scene change");
}

void testRefusals() {
    const struct {
        const char* what;
        rule::Conditions conditions;
        Refusal refusal;
    } cases[] = {
        {"a door running", [] { auto c = allowed(); c.doorActive = true; return c; }(), Refusal::Door},
        {"a menu open", [] { auto c = allowed(); c.menuOpen = true; return c; }(), Refusal::Menu},
        {"a boss alive", [] { auto c = allowed(); c.bossAlive = true; return c; }(), Refusal::Boss},
        {"no last door into the scene", [] { auto c = allowed(); c.hasDoor = false; return c; }(), Refusal::NoDoor},
    };
    for (const auto& item : cases) {
        rule::Ladder ladder;
        ladder.press(kStartMs, kSceneA, allowed());
        const rule::Decision decision = ladder.press(kStartMs + 1000, kSceneA, item.conditions);
        char what[96];
        std::snprintf(what, sizeof(what), "%s refuses the reload and falls to the toast", item.what);
        check(decision.action == Action::Toast && decision.refusal == item.refusal, what);
    }
    rule::Ladder both;
    both.press(kStartMs, kSceneA, allowed());
    auto conditions = allowed();
    conditions.doorActive = true;
    conditions.bossAlive = true;
    check(both.press(kStartMs + 1000, kSceneA, conditions).refusal == Refusal::Door, "the door is named before the boss");
    check(std::strcmp(rule::name(Refusal::NoDoor), "no door into this scene") == 0, "refusal names");
}

void testSecondReload() {
    rule::Ladder ladder;
    ladder.press(kStartMs, kSceneA, allowed());
    check(ladder.press(kStartMs + 1000, kSceneA, allowed()).action == Action::Reload, "the first reload runs");
    const uint32_t later = kStartMs + 2 * rule::kLadderResetMs;
    check(ladder.press(later, kSceneA, allowed()).action == Action::Release, "after the ladder resets it releases again");
    const rule::Decision again = ladder.press(later + 1000, kSceneA, allowed());
    check(again.action == Action::Toast && again.refusal == Refusal::AlreadyReloaded, "but the second reload in the scene is refused");
    ladder.onScene(kSceneB);
    ladder.onScene(kSceneA);
    ladder.press(later + 2000, kSceneA, allowed());
    check(ladder.press(later + 3000, kSceneA, allowed()).action == Action::Reload, "after the player was elsewhere the scene may be reloaded again");
}

void testOrphanLock() {
    constexpr uint32_t kBits = 0x2;
    rule::OrphanLock lock;
    check(!lock.observe(kStartMs, kBits, false, true), "bits set start the count");
    check(!lock.observe(kStartMs + rule::kOrphanMs - 1, kBits, false, true), "just short of four seconds does nothing");
    check(lock.observe(kStartMs + rule::kOrphanMs, kBits, false, true), "four seconds with no script release them");
    check(!lock.observe(kStartMs + rule::kOrphanMs + 1, kBits, false, true), "once");
    check(!lock.observe(kStartMs + 2 * rule::kOrphanMs - 1, kBits, false, true), "the bits that stay are given another four seconds");
    check(lock.observe(kStartMs + 2 * rule::kOrphanMs, kBits, false, true), "then released again");

    rule::OrphanLock clear;
    clear.observe(kStartMs, kBits, false, true);
    check(!clear.observe(kStartMs + 3000, 0, false, true), "bits that went away reset the count");
    check(!clear.observe(kStartMs + 3500, kBits, false, true), "and they start it again");
    check(!clear.observe(kStartMs + 3500 + rule::kOrphanMs - 1, kBits, false, true), "from the new start");

    rule::OrphanLock thread;
    thread.observe(kStartMs, kBits, false, true);
    thread.observe(kStartMs + 3000, kBits, true, true);
    check(!thread.observe(kStartMs + rule::kOrphanMs, kBits, false, true), "a script thread restarts the count");
    check(thread.observe(kStartMs + 3000 + rule::kOrphanMs + 1, kBits, false, true) == false, "not before four seconds after it ended");
    check(thread.observe(kStartMs + rule::kOrphanMs + rule::kOrphanMs, kBits, false, true), "but four seconds after it ended");

    rule::OrphanLock phase;
    phase.observe(kStartMs, kBits, false, true);
    phase.observe(kStartMs + 3000, kBits, false, false);
    check(!phase.observe(kStartMs + rule::kOrphanMs, kBits, false, true), "leaving plain gameplay restarts the count");
    check(phase.observe(kStartMs + rule::kOrphanMs + rule::kOrphanMs, kBits, false, true), "four seconds after it returned");
}

void testReleasePlan() {
    constexpr int32_t kDepth = 1;
    check(rule::releasePlan(rule::kEventPhase, kDepth, rule::kNoPhase, false) == rule::Release::PopEvent, "an event over a stacked phase is popped");
    check(rule::releasePlan(rule::kEventPhase, 0, rule::kNoPhase, false) == rule::Release::Nothing, "never at depth 0");
    check(rule::releasePlan(rule::kEventPhase, -1, rule::kNoPhase, false) == rule::Release::Nothing, "nor below it");
    check(rule::releasePlan(rule::kEventPhase, kDepth, 1, false) == rule::Release::Nothing, "never with a phase requested");
    check(rule::releasePlan(rule::kEventPhase, kDepth, rule::kNoPhase, true) == rule::Release::Nothing, "never under a running script");
    check(rule::releasePlan(1, kDepth, rule::kNoPhase, false) == rule::Release::Nothing, "only the Event phase is popped");
}

}  // namespace

int main() {
    testChord();
    testLadderOrder();
    testLadderResets();
    testRefusals();
    testSecondReload();
    testOrphanLock();
    testReleasePlan();
    if (g_failures == 0) std::printf("rescue_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
