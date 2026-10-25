// Checks the enemy observers' pure rules (no game): the construction gate, which enemies are observed and scoped, which
// stream entries apply to which local enemy, request number extension, and one enemy's timeline of samples (render
// delay, interpolation, hold, hard resets, stale), and the phase 8 rules: role changes and hand-over starts, hit routing and
// request fate, creation on demand and its pacing. Exit 0 when every check passes.
#include <cmath>
#include <array>
#include <cstdio>
#include <optional>
#include <string>

#include "../src/enemy_observer_rule.h"

namespace {

namespace rule = enemy_observer_rule;
namespace wire = enemy_observer_wire;
using rule::Policy;

constexpr uint32_t kArrivalDelayMs = 500;  // the test link's transit time
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

bool closeTo(float a, float b) { return std::fabs(a - b) < 0.01f; }

void testConstructionGate() {
    rule::ConstructionGate gate;
    check(!gate.open(), "a new enemy is not constructed");
    gate.note(rule::kSetupState);
    check(!gate.open(), "the setup state keeps the gate closed");
    gate.note(2);
    check(gate.open(), "a record past setup opens the gate");
    gate.note(1);
    check(gate.open(), "it stays open while the record is past setup");
    gate.note(rule::kSetupState);
    check(!gate.open(), "an enemy created again in the same place closes it again");
}

void testObservedAndScoped() {
    check(rule::observed(Policy::Observer, true), "an Observer class is observed when the peer runs the room");
    check(!rule::observed(Policy::Observer, false), "nothing is observed when this machine runs the room");
    check(!rule::observed(Policy::Legacy, true), "a Legacy class keeps the decision replay");
    check(!rule::observed(Policy::Native, true), "a Native class runs natively");
    check(rule::scoped(true, true, false), "an observed, constructed enemy is scoped");
    check(!rule::scoped(true, false, false), "construction runs natively first");
    check(!rule::scoped(true, true, true), "a removed enemy is the class's own again");
    check(!rule::scoped(false, true, false), "an enemy that is not observed is never scoped");
}

void testStreamAcceptance() {
    const wire::Header header = {0, 1, 7, 0x12, 1, 0};
    check(rule::refusalOf(header, 0, 0, 7, 0x12) == rule::Refusal::None, "the holder's batch for the loaded scene counts");
    check(rule::refusalOf(header, 1, 0, 7, 0x12) == rule::Refusal::Sender, "a batch from another slot is refused");
    check(rule::refusalOf(header, 0, 0, 7, 0x13) == rule::Refusal::Scene, "a batch for another scene is refused");
    check(rule::refusalOf(header, 0, 0, 8, 0x12) == rule::Refusal::Epoch, "a batch from an old epoch is refused");
    check(std::string(rule::name(rule::Refusal::Epoch)) == "epoch", "refusal name");
}

void testBinding() {
    wire::PoseEntry entry{};
    entry.classIndex = 4;
    entry.spawnId = 300;
    entry.spawnRecord = 77;
    check(rule::bound({4, 300, 77}, entry), "the same class, spawn id and record bind");
    check(!rule::bound({5, 300, 77}, entry), "another class does not bind");
    check(!rule::bound({4, 301, 77}, entry), "another spawn id does not bind");
    check(!rule::bound({4, 300, 78}, entry), "another record does not bind");
    const int32_t setup[4] = {0, 1, 0, 0};
    const int32_t running[4] = {2, 1, 0, 0};
    check(!rule::recordApplies(setup), "a record in setup is never written");
    check(rule::recordApplies(running), "a running record is written");
}

void testBindingByRecord() {
    constexpr uint16_t kRecord = 77;
    std::array<std::optional<enemy_spawn::Origin>, 12> origins{};
    origins[9] = enemy_spawn::Origin{kRecord, 300};
    origins[3] = enemy_spawn::Origin{kRecord + 1, 301};
    wire::PoseEntry entry{};
    entry.slot = 5;  // the simulator keeps this enemy in another pool slot than this machine does
    entry.classIndex = 4;
    entry.spawnId = 300;
    entry.spawnRecord = kRecord;
    const int local = rule::slotOfRecord(origins, entry.spawnRecord);
    check(local == 9 && local != entry.slot, "an entry binds to the enemy of its record, not to the equal pool slot");
    check(local != rule::kNoPoolSlot && rule::bound({4, 300, kRecord}, entry), "and applies to it");
    entry.slot = 9;  // the equal pool slot holds another record
    entry.spawnRecord = kRecord + 2;
    check(rule::slotOfRecord(origins, entry.spawnRecord) == rule::kNoPoolSlot, "a record no local enemy was created from binds to nothing");
    entry.spawnRecord = kRecord + 1;
    check(rule::slotOfRecord(origins, entry.spawnRecord) == 3 && !rule::bound({4, 300, kRecord}, entry),
          "another record in the same pool slot is refused");
}

void testLife() {
    check(rule::lifeOf(wire::kFlagAlive) == rule::Life::Alive, "alive");
    check(rule::lifeOf(wire::kFlagTerminal) == rule::Life::Dead, "terminal is dead");
    check(rule::lifeOf(wire::kFlagTerminal | wire::kFlagRemoved) == rule::Life::Gone, "removed wins");
    check(rule::lifeOf(wire::kFlagAlive | wire::kFlagLeased) == rule::Life::Alive, "a lease is still alive");
}

void testSequenceExtension() {
    check(rule::extendSeq(0x00020010, 0x0011) == 0x00020011, "the next request in the same half");
    check(rule::extendSeq(0x0001fffe, 0x0001) == 0x00020001, "wrapping the low 16 bits forward");
    check(rule::extendSeq(0x00020001, 0xffff) == 0x0001ffff, "a late request from before the wrap");
    check(rule::extendSeq(0xfffffffe, 0x0001) == 0x00000001, "wrapping the 32 bits");
    check(rule::extendSeq(5, 5) == 5, "the same number");
}

void testSampleAndItemConversion() {
    wire::PoseEntry entry{};
    entry.flags = wire::kFlagTerminal;
    entry.hp = -1;
    entry.motion0 = 9;
    entry.frame0 = 4.5f;
    entry.frame1 = pose_wire::packFrame(2.0f);
    const float quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    entry.quat = pose_wire::packQuat(quat);
    entry.record[1] = 5;
    const wire::Header header = {900, 12, 3, 0x20, 1, 0};
    const rule::Sample sample = rule::sampleOf(header, entry, 1500);
    check(sample.senderMs == 900 && sample.seq == 12 && sample.arrivalMs == 1500, "sample timing and number come from the header");
    check(sample.hp == -1 && sample.motion0 == 9 && closeTo(sample.frame0, 4.5f) && closeTo(sample.frame1, 2.0f) && sample.record[1] == 5,
          "sample fields");
    check(closeTo(sample.quat[3], 1.0f), "the rotation unpacks");

    const wire::MotionEntry motion = {3, 2, 0x44, 0x10, 0.5f, 6.0f, 40, 0};
    const rule::MotionItem item = rule::itemOf(header, motion, 77);
    check(item.senderMs == 860 && item.seq == 77 && item.layer == 2 && item.motionNo == 0x44 && item.flags == 0x10 &&
              closeTo(item.interp, 0.5f) && closeTo(item.start, 6.0f),
          "a motion request happened dtMs before the batch");
}

stream_clock::StreamClock readyClock() {
    stream_clock::StreamClock clock;
    clock.observe(1000, 1000 + kArrivalDelayMs);
    return clock;
}

// The local time at which the clock shows `senderMs`.
uint32_t nowFor(uint32_t senderMs) { return senderMs + kArrivalDelayMs + stream_clock::kMinDelayMs; }

rule::Sample sampleAt(uint32_t senderMs, uint32_t seq, float x, uint8_t flags = wire::kFlagAlive) {
    rule::Sample sample;
    sample.senderMs = senderMs;
    sample.arrivalMs = senderMs + kArrivalDelayMs;
    sample.seq = seq;
    sample.flags = flags;
    sample.pos[0] = x;
    sample.hp = static_cast<int16_t>(100 + seq);
    sample.record[0] = static_cast<int32_t>(seq);
    return sample;
}

void testRenderDelayAndInterpolation() {
    rule::SlotTimeline timeline;
    const stream_clock::StreamClock clock = readyClock();
    timeline.push(clock, sampleAt(1000, 1, 0.0f));
    timeline.push(clock, sampleAt(1050, 2, 50.0f));
    timeline.push(clock, sampleAt(1100, 3, 100.0f));
    check(!timeline.render(clock, nowFor(990)).valid, "nothing is shown before the first sample's time");
    const rule::Shown middle = timeline.render(clock, nowFor(1060));
    check(middle.valid && closeTo(middle.sample.pos[0], 60.0f), "position interpolates between the bracketing samples");
    check(middle.sample.hp == 102 && middle.sample.record[0] == 2, "discrete fields are the older sample's");
    check(middle.reset == rule::Reset::None && !middle.stale, "no reset, not stale");
    const rule::Shown past = timeline.render(clock, nowFor(1500));
    check(past.valid && closeTo(past.sample.pos[0], 100.0f), "past the newest sample the last one is held");
    const rule::Shown held = timeline.render(clock, nowFor(1510));
    check(held.valid && closeTo(held.sample.pos[0], 100.0f) && held.sample.hp == 103, "and keeps being held");
}

void testNewestAndAdopt() {
    rule::SlotTimeline timeline;
    const stream_clock::StreamClock clock = readyClock();
    check(!timeline.newest(), "a timeline with nothing pushed has no newest sample");
    timeline.push(clock, sampleAt(1000, 1, 0.0f));
    timeline.push(clock, sampleAt(1050, 2, 50.0f));
    timeline.push(clock, sampleAt(1100, 3, 100.0f));
    const rule::Shown shown = timeline.render(clock, nowFor(1060));
    check(shown.valid && closeTo(shown.sample.pos[0], 60.0f), "the rendered sample lags behind");
    const std::optional<rule::Sample> newest = timeline.newest();
    check(newest && newest->seq == 3 && closeTo(newest->pos[0], 100.0f) && newest->hp == 103,
          "newest is the last sample pushed, not the one rendered");
    check(timeline.newest()->arrivalMs == 1100 + kArrivalDelayMs, "newest keeps its arrival time for the age check");
    timeline.push(clock, sampleAt(1075, 4, 75.0f));
    check(timeline.newest()->seq == 3, "a late sample does not become the newest");

    const uint32_t arrival = 5000;
    check(rule::adoptable(arrival, arrival), "a sample that just arrived is adopted");
    check(rule::adoptable(arrival + rule::kAdoptMaxAgeMs, arrival), "one exactly kAdoptMaxAgeMs old is adopted");
    check(!rule::adoptable(arrival + rule::kAdoptMaxAgeMs + 1, arrival), "an older one falls back to the enemy's own state");
    check(rule::adoptable(10, 0xfffffff0u), "the age check survives the millisecond clock wrapping");
}

void testDuplicatesAndOrder() {
    rule::SlotTimeline timeline;
    const stream_clock::StreamClock clock = readyClock();
    timeline.push(clock, sampleAt(1100, 3, 100.0f));
    timeline.push(clock, sampleAt(1000, 1, 0.0f));
    timeline.push(clock, sampleAt(1000, 1, 999.0f));
    const rule::Shown shown = timeline.render(clock, nowFor(1020));
    check(shown.valid && closeTo(shown.sample.pos[0], 20.0f) && shown.buffered == 2, "late samples are ordered and a repeated number is dropped");
}

void testHardResets() {
    const stream_clock::StreamClock clock = readyClock();

    rule::SlotTimeline gap;
    gap.push(clock, sampleAt(1000, 1, 0.0f));
    gap.push(clock, sampleAt(1700, 2, 5.0f));
    const rule::Shown afterGap = gap.render(clock, nowFor(1000));
    check(afterGap.valid && afterGap.reset == rule::Reset::Gap && closeTo(afterGap.sample.pos[0], 5.0f),
          "a gap over 500 ms places the newest sample at once");
    check(gap.render(clock, nowFor(1010)).reset == rule::Reset::None, "the reset is reported once");

    rule::SlotTimeline jump;
    jump.push(clock, sampleAt(1000, 1, 0.0f));
    jump.push(clock, sampleAt(1050, 2, pose_timeline::kTeleportUnits + 1.0f));
    const rule::Shown afterJump = jump.render(clock, nowFor(1000));
    check(afterJump.valid && afterJump.reset == rule::Reset::Teleport, "a jump of 2000 units is a teleport");

    rule::SlotTimeline restarted;
    stream_clock::StreamClock shared = readyClock();
    restarted.push(shared, sampleAt(1000, 1, 0.0f));
    shared.reanchor(kArrivalDelayMs);
    restarted.push(shared, sampleAt(1050, 2, 7.0f));
    const rule::Shown afterClock = restarted.render(shared, nowFor(1000));
    check(afterClock.valid && afterClock.reset == rule::Reset::Clock && closeTo(afterClock.sample.pos[0], 7.0f),
          "a re-anchored shared clock drops the old mapping and places the newest sample");
}

void testDeadEnemyIsSampledRarely() {
    rule::SlotTimeline timeline;
    const stream_clock::StreamClock clock = readyClock();
    timeline.push(clock, sampleAt(1000, 1, 0.0f, wire::kFlagTerminal));
    timeline.push(clock, sampleAt(2000, 2, 0.0f, wire::kFlagTerminal));
    const rule::Shown shown = timeline.render(clock, nowFor(2010));
    check(shown.valid && shown.reset == rule::Reset::None, "a second between samples of a corpse is no gap");
    check(rule::lifeOf(shown.sample.flags) == rule::Life::Dead, "the corpse stays dead");
}

void testStale() {
    rule::SlotTimeline timeline;
    const stream_clock::StreamClock clock = readyClock();
    timeline.push(clock, sampleAt(1000, 1, 0.0f));
    const uint32_t lastArrival = 1000 + kArrivalDelayMs;
    check(!timeline.render(clock, lastArrival + pose_timeline::kStaleMs).stale, "2000 ms of silence is not yet stale");
    const rule::Shown stale = timeline.render(clock, lastArrival + pose_timeline::kStaleMs + 1);
    check(stale.stale && !stale.valid, "over 2000 ms of silence is stale: nothing is written");
    timeline.push(clock, sampleAt(4000, 2, 3.0f));
    check(!timeline.render(clock, 4000 + kArrivalDelayMs + 10).stale, "a new sample ends the staleness");
}

void testBufferIsBounded() {
    rule::SlotTimeline timeline;
    const stream_clock::StreamClock clock = readyClock();
    for (uint32_t i = 0; i < rule::kMaxSamples + 10; ++i) timeline.push(clock, sampleAt(1000 + i * 50, i + 1, static_cast<float>(i)));
    const rule::Shown shown = timeline.render(clock, nowFor(1000 + 10 * 50));
    check(shown.valid && shown.buffered == rule::kMaxSamples, "no more than kMaxSamples are kept, the oldest go first");
}


rule::Role roleOf(bool observing, bool simulating, uint16_t scene, uint8_t holder, uint32_t epoch) {
    return {observing, simulating, scene, holder, epoch};
}

void testRoleChange() {
    const rule::Role observing = roleOf(true, false, 0x12, 1, 3);
    const rule::Role simulating = roleOf(false, true, 0x12, 0, 4);
    const rule::Role nobody = roleOf(false, false, 0x12, 1, 3);
    check(rule::roleChange(observing, observing, 0) == rule::RoleChange::None, "no change is no change");
    check(rule::roleChange(observing, simulating, 0) == rule::RoleChange::HandOver,
          "the room we were observing is now ours: a hand-over");
    check(rule::roleChange(observing, nobody, 0) == rule::RoleChange::Wait,
          "the simulator is gone and nobody holds the room yet: wait");
    check(rule::roleChange(observing, nobody, rule::kHandOverWaitMs - 1) == rule::RoleChange::Wait, "still waiting just before the limit");
    check(rule::roleChange(observing, nobody, rule::kHandOverWaitMs) == rule::RoleChange::Restart, "the wait runs out: start over");
    check(rule::roleChange(observing, roleOf(true, false, 0x12, 1, 4), 0) == rule::RoleChange::Restart, "a new epoch of the same holder is a new stream");
    check(rule::roleChange(observing, roleOf(false, true, 0x13, 0, 1), 0) == rule::RoleChange::Restart,
          "another room is never a hand-over of this one");
    check(rule::roleChange(nobody, simulating, 0) == rule::RoleChange::Restart, "a room that was not observed has nothing to hand over");
    check(rule::roleChange(simulating, nobody, 0) == rule::RoleChange::Restart, "losing the room starts over");
}

void testHandOverStart() {
    const enemy_follow_rule::Action action = {{1, 8, 3, 1}};
    const enemy_follow_rule::Action think = {{enemy_follow_rule::kThinkState, 0, 0, 0}};
    const enemy_follow_rule::Action setup = {{enemy_follow_rule::kSetupState, 0, 0, 0}};
    check(rule::handOverStart(80, action) == rule::Start::Resume, "a living enemy in an action starts it again at step 0");
    check(rule::handOverStart(80, think) == rule::Start::Think, "an enemy in its think state lets its own think pick");
    check(rule::handOverStart(80, setup) == rule::Start::Think, "an enemy still in setup is left to its setup");
    check(rule::handOverStart(0, action) == rule::Start::Corpse, "a dead enemy is not restarted");
    check(rule::handOverStart(-1, action) == rule::Start::Corpse, "HP -1 is dead");
    check(enemy_follow_rule::startOf(action).word[2] == 0, "the action restarts at its first step");
}

void testHitRouting() {
    const rule::Simulator here{true, true, 0};
    const rule::Simulator peer{true, false, 1};
    const rule::Simulator nobody{};
    check(rule::routeOfOwnHit(here) == rule::Route::Apply, "a hit on an enemy simulated here runs here");
    check(rule::routeOfOwnHit(peer) == rule::Route::Send, "a hit on an enemy simulated by the peer goes to it");
    check(rule::routeOfOwnHit(nobody) == rule::Route::Local, "with nobody simulating the room the hit stays local");
    check(rule::fateOfRequest(here, false) == rule::Fate::Apply, "a request for an enemy simulated here is applied");
    check(rule::fateOfRequest(here, true) == rule::Fate::Apply, "a forwarded request that lands on the simulator is applied");
    check(rule::fateOfRequest(peer, false) == rule::Fate::Forward, "a request for an enemy simulated elsewhere is sent on once");
    check(rule::fateOfRequest(peer, true) == rule::Fate::Drop, "a request already sent on is dropped");
    check(rule::fateOfRequest(nobody, false) == rule::Fate::Drop, "a request nobody can take is dropped");
}

size_t classWithPolicy(Policy policy) {
    for (size_t i = 0; i < enemy_policy::kClassCount; ++i) {
        if (enemy_policy::kClasses[i].policy == policy) return i;
    }
    return enemy_policy::kClassCount;
}

void testCreateWanted() {
    wire::PoseEntry entry{};
    entry.classIndex = static_cast<uint8_t>(classWithPolicy(Policy::Observer));
    entry.flags = wire::kFlagAlive;
    check(rule::createWanted(false, entry), "a living Observer enemy whose record no local enemy has is created");
    check(!rule::createWanted(true, entry), "a record that exists in some pool slot is not created again");
    entry.flags = wire::kFlagAlive | wire::kFlagLeased;
    check(rule::createWanted(false, entry), "a leased enemy is still living");
    entry.flags = wire::kFlagTerminal;
    check(!rule::createWanted(false, entry), "a corpse is not created");
    entry.flags = wire::kFlagRemoved;
    check(!rule::createWanted(false, entry), "a removed enemy is not created");
    entry.flags = wire::kFlagAlive;
    entry.classIndex = static_cast<uint8_t>(classWithPolicy(Policy::Legacy));
    check(!rule::createWanted(false, entry), "a Legacy class keeps its own spawn");
}

void testCreatePacer() {
    rule::CreatePacer pacer;
    pacer.beginFrame();
    for (size_t slot = 0; slot < rule::kMaxCreatesPerFrame; ++slot) check(pacer.allow(slot, 1000), "four creations fit one frame");
    check(!pacer.allow(10, 1000), "the fifth waits for the next frame");
    pacer.beginFrame();
    check(pacer.allow(10, 1016), "the next frame has room again");
    check(!pacer.allow(0, 1016), "a slot created a moment ago waits");
    check(!pacer.allow(0, 1000 + rule::kCreateSlotIntervalMs - 1), "a slot waits the full second");
    check(pacer.allow(0, 1000 + rule::kCreateSlotIntervalMs), "a second later it may be created again");
    check(!pacer.allow(200, 5000), "a slot beyond the table is refused");
    pacer.clear();
    pacer.beginFrame();
    check(pacer.allow(0, 1000), "a new room starts the pacing over");
}

}  // namespace

int main() {
    testConstructionGate();
    testObservedAndScoped();
    testStreamAcceptance();
    testBinding();
    testBindingByRecord();
    testLife();
    testSequenceExtension();
    testSampleAndItemConversion();
    testRenderDelayAndInterpolation();
    testNewestAndAdopt();
    testDuplicatesAndOrder();
    testHardResets();
    testDeadEnemyIsSampledRarely();
    testStale();
    testBufferIsBounded();
    testRoleChange();
    testHandOverStart();
    testHitRouting();
    testCreateWanted();
    testCreatePacer();
    if (g_failures == 0) std::printf("enemy_observer_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
