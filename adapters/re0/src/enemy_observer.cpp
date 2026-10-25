#include "enemy_observer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <vector>

#include "character_owner.h"
#include "debug_stats.h"
#include "door_leave_rule.h"
#include "door_travel.h"
#include "enemy_decision.h"
#include "enemy_drift.h"
#include "enemy_follow_rule.h"
#include "enemy_lease.h"
#include "enemy_observer_capture.h"
#include "enemy_observer_game.h"
#include "enemy_observer_rule.h"
#include "enemy_observer_wire.h"
#include "enemy_policy.h"
#include "enemy_registry.h"
#include "enemy_spawn.h"
#include "frame_router.h"
#include "game.h"
#include "game_state.h"
#include "hit_fx.h"
#include "game_tick.h"
#include "hooks.h"
#include "log.h"
#include "net_pad.h"
#include "pose_game.h"
#include "pose_wire.h"
#include "puppet.h"
#include "puppet_rule.h"
#include "room_authority.h"
#include "scene.h"
#include "split_rooms.h"
#include "stream_clock.h"
#include "timed_queue.h"

namespace {

namespace rule = enemy_observer_rule;
namespace wire = enemy_observer_wire;
using debug_stats::Counter;

constexpr size_t kMaxPendingBatches = 64;  // batches waiting for the game thread
constexpr uint32_t kLogIntervalMs = 1000;
constexpr int kNoIndex = rule::kNoPoolSlot;
constexpr float kNoSeekInterp = 0.0f;  // a seek restarts the motion at the simulator's frame without blending
constexpr size_t kFollowedLayers = 2;  // the body (0) and the overlay (1), the two layers ENEMY_POSE carries

using UnitMoveFunction = void(__fastcall*)(void* unit, void* edx);
UnitMoveFunction g_originalUnitMove = nullptr;
std::atomic<bool> g_enabled{false};

struct PoseArrival {
    wire::PoseBatch batch;
    uint8_t sender;
    uint32_t arrivalMs;
};
struct MotionArrival {
    wire::MotionBatch batch;
    uint8_t sender;
};

std::mutex g_mutex;
std::vector<PoseArrival> g_poses;      // guarded by g_mutex: net thread to game thread
std::vector<MotionArrival> g_motions;  // guarded by g_mutex

// Everything below is the game thread's.

// How the loaded room stands this frame.
struct World {
    bool observing = false;   // the peer runs the room and shares it: its enemies are shown from the stream
    bool simulating = false;  // this machine runs the room with the peer in it: it sends the stream
    uint16_t scene = scene::kNone;
    uint8_t holderSlot = 0;
    uint32_t epoch = 0;
    std::optional<stream_clock::StreamClock> clock;  // the holder's clock; nothing before its first pose
    uint32_t nowMs = 0;
    std::array<uintptr_t, game::kEnemyPoolSlots> pool{};
    std::array<std::optional<enemy_spawn::Origin>, game::kEnemyPoolSlots> origin{};  // the spawn record of each pool enemy
};
World g_world;

enum class Hold : uint8_t { None, Construction, NoData, Stale, Binding };

const char* name(Hold hold) {
    switch (hold) {
        case Hold::Construction: return "construction";
        case Hold::NoData: return "no data";
        case Hold::Stale: return "stale";
        case Hold::Binding: return "binding";
        case Hold::None: break;
    }
    return "none";
}

// What this machine keeps about the enemy in one pool slot; it starts over when the slot holds another enemy.
struct Slot {
    uintptr_t enemy = 0;
    bool observer = false;  // an Observer-class enemy whose spawn is known and that a stream shows
    enemy_lease::Role role = enemy_lease::Role::None;  // Away: shown from the lease holder's stream; Held: simulated here
    uint8_t source = 0;                               // the machine whose stream shows it
    std::optional<stream_clock::StreamClock> clock;   // that machine's clock; nothing before its first message
    uint32_t targetMs = 0;                            // the source's time to show
    rule::LocalEnemy local;
    rule::ConstructionGate gate;
    bool released = false;         // the simulator removed the unit: its own code takes it out
    uintptr_t frozenEnemy = 0;     // freeze() holds this enemy under the CharPause scope whoever simulates it
    std::optional<rule::Sample> written;  // what drive wrote into the enemy before this frame's unit move (enemy_drift compares after it)
    bool announced = false;
    bool terminalLogged = false;
    int streamSlot = kNoIndex;     // the simulator's pool slot of the enemy it shows this one from (hits name it)
    bool bindingRefused = false;   // a stream entry for this enemy's spawn record named another class or spawn id
    Hold hold = Hold::None;
    uint32_t holdSinceMs = 0;
    rule::SlotTimeline timeline;
    timed_queue::TimedQueue<rule::MotionItem> motions;
    std::array<puppet_rule::SeekTracker, kFollowedLayers> seek{};
    std::array<uint32_t, kFollowedLayers> layerFlags{};  // flags of the last request on each layer, reused by its seek
};
std::array<Slot, game::kEnemyPoolSlots> g_slots;
uint32_t g_motionSeq = 0;  // newest request number seen on the stream
bool g_motionSeqKnown = false;
uint32_t g_nextLogMs = 0;
bool g_logStarted = false;
std::vector<rule::MotionItem> g_due;
rule::CreatePacer g_pacer;
rule::MissingWatch g_missing;
door_leave_rule::Departure g_departure;  // the final pose round goes out at the first tick of this machine's door out of its room
bool g_slotsLive = false;  // the pool slots below describe this frame (a stream shows some enemy of the room)
bool g_waiting = false;  // the observed room lost its simulator and nobody holds it yet
uint32_t g_waitSinceMs = 0;

void noteHold(size_t index, Slot& slot, Hold hold) {
    if (slot.hold == hold) return;
    const Hold before = slot.hold;
    slot.hold = hold;
    if (hold == Hold::None) {
        if (before == Hold::Stale) logger::write("enemy_observer: slot %zu resumed after %u ms", index, g_world.nowMs - slot.holdSinceMs);
        return;
    }
    slot.holdSinceMs = g_world.nowMs;
    if (hold == Hold::Stale) debug_stats::count(Counter::EnemyObserverStale);
    logger::write("enemy_observer: slot %zu held (%s)", index, name(hold));
}

// ---- Net thread --------------------------------------------------------------------------------------------------

template <typename Arrival>
void queue(std::vector<Arrival>& waiting, Arrival&& arrival) {
    std::lock_guard lock(g_mutex);
    if (waiting.size() >= kMaxPendingBatches) waiting.erase(waiting.begin());
    waiting.push_back(std::move(arrival));
}

void onFrame(const GameFrame& frame) {
    if (!g_enabled) return;
    if (frame.type == wire::kMsgEnemyPose) {
        if (auto batch = wire::decode<wire::PoseEntry>(frame.payload)) {
            return queue(g_poses, PoseArrival{std::move(*batch), frame.slot, pose_wire::clockMs()});
        }
    } else if (auto batch = wire::decode<wire::MotionEntry>(frame.payload)) {
        return queue(g_motions, MotionArrival{std::move(*batch), frame.slot});
    }
    debug_stats::count(Counter::EnemyObserverRefused);
    logger::writeUnlessRepeated("enemy_observer: type 0x%04x from slot %u refused malformed", frame.type, frame.slot);
}

// ---- Game thread: the world --------------------------------------------------------------------------------------

void dropPending() {
    std::lock_guard lock(g_mutex);
    g_poses.clear();
    g_motions.clear();
}

void resetStreams() {
    for (Slot& slot : g_slots) slot = Slot{};
    g_motionSeqKnown = false;
    g_pacer.clear();
    g_missing.clear();
    dropPending();
}

World readWorld() {
    World next;
    next.scene = scene::current();
    next.nowMs = pose_wire::clockMs();
    const std::optional<room_authority::Holder> holder =
        next.scene == scene::kNone ? std::nullopt : room_authority::holderOf(next.scene);
    const bool together = holder && net_pad::active() && !split_rooms::apart();
    next.observing = together && !holder->here;
    next.simulating = together && holder->here;
    if (holder) {
        next.holderSlot = holder->slot;
        next.epoch = holder->epoch;
    }
    if (next.observing) next.clock = puppet::clockFor(next.holderSlot);
    for (size_t slot = 0; slot < next.pool.size(); ++slot) {
        next.pool[slot] = enemy_registry::enemyAt(static_cast<int>(slot));
        next.origin[slot] = enemy_spawn::originOf(next.pool[slot]);
    }
    return next;
}

rule::Role roleOf(const World& world) {
    return {world.observing, world.simulating, world.scene, world.holderSlot, world.epoch};
}

// Carries an enemy that was shown from the stream on as this machine's own, from its HP and the behaviour record `words`:
// a living one in an action starts that action again at step 0 through the class's own setAction, one in its think state
// lets its own think pick the next, a dead one plays out.
rule::Start carryOn(uintptr_t enemy, int16_t hp, const int32_t (&words)[4]) {
    enemy_follow_rule::Action record;
    std::copy(std::begin(words), std::end(words), std::begin(record.word));
    const rule::Start start = rule::handOverStart(hp, record);
    if (start == rule::Start::Resume) enemy_decision::startAction(enemy, enemy_follow_rule::startOf(record));
    return start;
}

// The newest sample received for a hand-over to adopt, when it is fresh enough.
std::optional<rule::Sample> adoptableSample(const Slot& slot, uint32_t nowMs) {
    std::optional<rule::Sample> newest = slot.timeline.newest();
    if (!newest || !rule::adoptable(nowMs, newest->arrivalMs) || rule::lifeOf(newest->flags) == rule::Life::Gone) return std::nullopt;
    return newest;
}

// This machine now runs the room it was observing. Every scoped enemy leaves the CharPause path (the new world no
// longer observes) and carries on from the newest sample the simulator sent (the final round it sends at its door
// included), or from its own state when that sample is old or missing.
void handOver(const World& next) {
    unsigned resumed = 0;
    unsigned atStepZero = 0;
    unsigned adopted = 0;
    for (size_t index = 0; index < g_slots.size(); ++index) {
        const Slot& slot = g_slots[index];
        const uintptr_t enemy = next.pool[index];
        if (slot.enemy != enemy || !rule::scoped(slot.observer, slot.gate.open(), slot.released)) continue;
        rule::Start start;
        if (const std::optional<rule::Sample> sample = adoptableSample(slot, next.nowMs)) {
            enemy_observer_game::write(enemy, *sample);
            start = carryOn(enemy, sample->hp, sample->record);
            ++adopted;
        } else {
            wire::PoseEntry state{};
            if (!enemy_observer_game::read(enemy, state)) continue;
            start = carryOn(enemy, state.hp, state.record);
        }
        ++resumed;
        if (start == rule::Start::Resume) ++atStepZero;
    }
    debug_stats::count(Counter::EnemyHandOvers, resumed);
    logger::write("enemy_observer: authority -> here (scene 0x%02x e%u), %u enemies resumed (%u at step 0, adopted %u)",
                  static_cast<unsigned>(next.scene), next.epoch, resumed, atStepZero, adopted);
}

// The enemies this machine gave away leave their stream scope and carry on from the state they were last shown in (only
// within the same room: after a room change they are gone); every lease and queued grant is dropped (the room changed,
// the peer left or the session ended).
void releaseLeases(const World& next) {
    unsigned resumed = 0;
    for (size_t index = 0; index < g_slots.size() && next.scene == g_world.scene; ++index) {
        wire::PoseEntry state{};
        const uintptr_t enemy = next.pool[index];
        if (g_slots[index].role != enemy_lease::Role::Away || g_slots[index].enemy != enemy || !enemy_observer_game::read(enemy, state)) {
            continue;
        }
        carryOn(enemy, state.hp, state.record);
        ++resumed;
    }
    if (resumed > 0) logger::write("enemy_observer: %u enemies given away resumed", resumed);
    enemy_lease::reset();
}

// The room lost its simulator and nobody holds it yet: the world stays as it was, so the observed enemies keep their
// scope and hold their last motion until the claim arrives (or the wait runs out).
World heldWorld(World next) {
    if (!g_waiting) {
        g_waiting = true;
        g_waitSinceMs = next.nowMs;
        logger::write("enemy_observer: scene 0x%02x lost its simulator, holding", static_cast<unsigned>(g_world.scene));
    }
    next.observing = g_world.observing;
    next.simulating = false;
    next.scene = g_world.scene;
    next.holderSlot = g_world.holderSlot;
    next.epoch = g_world.epoch;
    next.clock = g_world.clock;
    return next;
}

// The same enemy built twice here: the stream drives the first, the second stays held (RE1-Coop parks it the same way).
void reportDuplicates(const World& world) {
    for (size_t slot = 0; slot < world.origin.size(); ++slot) {
        const int first = rule::duplicateOf(world.origin, slot);
        if (first == rule::kNoPoolSlot) continue;
        logger::writeUnlessRepeated("enemy_observer: slot %zu is record %u again (first in slot %d), held", slot,
                                    world.origin[slot]->record, first);
    }
}

void refreshWorld() {
    World next = readWorld();
    if (next.observing) reportDuplicates(next);
    const uint32_t waitedMs = g_waiting ? next.nowMs - g_waitSinceMs : 0;
    switch (rule::roleChange(roleOf(g_world), roleOf(next), waitedMs)) {
        case rule::RoleChange::None:
            g_waiting = false;
            break;
        case rule::RoleChange::Wait:
            next = heldWorld(std::move(next));
            break;
        case rule::RoleChange::HandOver:
            g_waiting = false;
            handOver(next);
            enemy_lease::reset();
            resetStreams();
            break;
        case rule::RoleChange::Restart:
            g_waiting = false;
            releaseLeases(next);
            resetStreams();
            if (next.observing) {
                logger::write("enemy_observer: showing scene 0x%02x from slot %u e%u", static_cast<unsigned>(next.scene), next.holderSlot, next.epoch);
            } else if (g_world.observing) {
                logger::write("enemy_observer: stopped showing scene 0x%02x", static_cast<unsigned>(g_world.scene));
            }
            break;
    }
    g_world = std::move(next);
}

// Each pool slot's enemy: the class, spawn id and spawn record it is bound by. A slot that holds another enemy starts over.
void refreshSlots() {
    for (size_t index = 0; index < g_slots.size(); ++index) {
        Slot& slot = g_slots[index];
        const uintptr_t enemy = g_world.pool[index];
        const int classIndex = enemy ? enemy_policy::classIndexOf(game::readPointer(enemy)) : enemy_policy::kNoClass;
        const std::optional<enemy_spawn::Origin>& origin = g_world.origin[index];
        const auto leaseSlot = static_cast<uint8_t>(index);
        const enemy_lease::Role role = enemy_lease::roleOf(leaseSlot);
        // The lease holder's stream shows an enemy given away; one held here is simulated natively; every other enemy is
        // shown from the room's simulator when the peer runs the room.
        const bool streamed = role == enemy_lease::Role::Away || (role == enemy_lease::Role::None && g_world.observing);
        const bool observer = origin && classIndex != enemy_policy::kNoClass &&
                              rule::observed(enemy_policy::kClasses[static_cast<size_t>(classIndex)].policy, streamed);
        const rule::LocalEnemy local = observer ? rule::LocalEnemy{static_cast<uint8_t>(classIndex), origin->spawnId, origin->record} : rule::LocalEnemy{};
        if (slot.enemy != enemy || slot.observer != observer || slot.local != local) {
            slot = Slot{};
            slot.enemy = enemy;
            slot.observer = observer;
            slot.local = local;
        }
        slot.role = role;
        slot.source = role == enemy_lease::Role::Away ? enemy_lease::peerOf(leaseSlot).value_or(g_world.holderSlot) : g_world.holderSlot;
        slot.clock = role == enemy_lease::Role::Away ? puppet::clockFor(slot.source) : g_world.clock;
        slot.targetMs = slot.clock ? slot.clock->target(g_world.nowMs) : 0;
    }
}

// ---- Game thread: draining the net thread's batches --------------------------------------------------------------

// False (and counted) when the batch is not from the machine that shows the enemy of `slot`: the slot that runs this
// room, or the holder of the lease on the enemy, under the table's epoch.
bool accepted(const wire::Header& header, uint8_t sender, const Slot& slot) {
    const rule::Refusal refusal = rule::refusalOf(header, sender, slot.source, g_world.epoch, g_world.scene);
    if (refusal == rule::Refusal::None) return true;
    debug_stats::count(Counter::EnemyObserverRefused);
    logger::writeUnlessRepeated("enemy_observer: batch from slot %u refused (%s)", sender, rule::name(refusal));
    return false;
}

// A living enemy of the stream whose spawn record no enemy here was created from is created natively from that record;
// the stream's entries apply from the frame after, once the refreshed slot binds the new enemy.
void createOnDemand(const wire::PoseEntry& entry) {
    if (g_pacer.allow(entry.slot, g_world.nowMs)) enemy_spawn::createOnDemand(entry.slot, entry.spawnRecord, entry.spawnId);
}

// The pool slot here of the enemy created from spawn record `record`, or kNoIndex.
int slotOfRecordHere(uint16_t record) { return rule::slotOfRecord(g_world.origin, record); }

// The pool slot here whose enemy is shown from the simulator's pool slot `streamSlot`, or kNoIndex.
int slotOfStream(uint8_t streamSlot) {
    for (size_t index = 0; index < g_slots.size(); ++index) {
        if (g_slots[index].observer && g_slots[index].streamSlot == streamSlot) return static_cast<int>(index);
    }
    return kNoIndex;
}

// The enemy of `slot` is shown from the simulator's pool slot `streamSlot`; no other enemy is.
void bindStream(Slot& slot, uint8_t streamSlot) {
    for (Slot& other : g_slots) {
        if (other.streamSlot == streamSlot) other.streamSlot = kNoIndex;
    }
    slot.streamSlot = streamSlot;
}

void takePose(const wire::Header& header, const wire::PoseEntry& entry, uint8_t sender, uint32_t arrivalMs) {
    const bool leased = (entry.flags & wire::kFlagLeased) != 0;
    const int local = slotOfRecordHere(entry.spawnRecord);
    if (local == kNoIndex) {
        // No enemy here was created from the record: the pool slot of the same number holds another enemy (or none), so
        // its lease role says nothing about this one; only the room's sender, scene and epoch decide.
        if (leased || rule::refusalOf(header, sender, g_world.holderSlot, g_world.epoch, g_world.scene) != rule::Refusal::None) return;
        if (rule::createWanted(false, entry)) createOnDemand(entry);
        if (rule::livingObserver(entry) && g_missing.missing(entry.spawnRecord, arrivalMs)) {
            debug_stats::count(Counter::EnemyMissing);
            logger::write("enemy_observer: record %u (%s) alive on the simulator, no enemy here for %u ms%s", entry.spawnRecord,
                          enemy_policy::nameOf(entry.classIndex), g_missing.missingForMs(entry.spawnRecord),
                          enemy_policy::bossClass(entry.classIndex) ? " (a boss: its room's script builds it)" : ", still creating");
        }
        return;
    }
    g_missing.present(entry.spawnRecord);
    Slot& slot = g_slots[static_cast<size_t>(local)];
    if (slot.role == enemy_lease::Role::Held) return;  // this machine simulates it; a pose of the authority sent before the grant is stale
    if (leased != (slot.role == enemy_lease::Role::Away)) {
        debug_stats::count(Counter::EnemyObserverRefused);
        return;
    }
    if (!accepted(header, sender, slot)) return;
    if (!slot.observer || !rule::bound(slot.local, entry)) {
        if (!slot.bindingRefused) {
            logger::write("enemy_observer: slot %u entry refused, stream %s/%u/record %u, here %s/%u/record %u",
                          static_cast<unsigned>(local),
                          enemy_policy::nameOf(entry.classIndex), entry.spawnId, entry.spawnRecord,
                          enemy_policy::nameOf(slot.local.classIndex), slot.local.spawnId, slot.local.record);
        }
        slot.bindingRefused = true;
        debug_stats::count(Counter::EnemyObserverRefused);
        return;
    }
    if (!slot.clock || (leased && !enemy_lease::acceptPose(entry, static_cast<uint8_t>(local), sender, arrivalMs))) return;
    slot.bindingRefused = false;
    bindStream(slot, entry.slot);
    slot.timeline.push(*slot.clock, rule::sampleOf(header, entry, arrivalMs));
    debug_stats::count(Counter::EnemyObserverPoses);
}

void drainPoses() {
    std::vector<PoseArrival> batches;
    {
        std::lock_guard lock(g_mutex);
        batches.swap(g_poses);
    }
    for (const PoseArrival& arrival : batches) {
        for (const wire::PoseEntry& entry : arrival.batch.entries) takePose(arrival.batch.header, entry, arrival.sender, arrival.arrivalMs);
    }
}

void takeMotion(const wire::Header& header, const wire::MotionEntry& entry, uint8_t sender) {
    const int local = slotOfStream(entry.slot);
    Slot& slot = g_slots[local == kNoIndex ? entry.slot : static_cast<size_t>(local)];
    if (slot.role == enemy_lease::Role::Held || !accepted(header, sender, slot)) return;
    const uint32_t seq = g_motionSeqKnown ? rule::extendSeq(g_motionSeq, entry.seqLow) : entry.seqLow;
    if (!g_motionSeqKnown || stream_clock::after(seq, g_motionSeq)) g_motionSeq = seq;
    g_motionSeqKnown = true;
    if (local != kNoIndex) slot.motions.push(rule::itemOf(header, entry, seq));
}

void drainMotions() {
    std::vector<MotionArrival> batches;
    {
        std::lock_guard lock(g_mutex);
        batches.swap(g_motions);
    }
    for (const MotionArrival& arrival : batches) {
        for (const wire::MotionEntry& entry : arrival.batch.entries) takeMotion(arrival.batch.header, entry, arrival.sender);
    }
}

// ---- Game thread: one enemy's unit move --------------------------------------------------------------------------

// Runs one simulator request on the enemy; skipped and counted when its motion banks lack the motion.
void replayMotion(size_t index, uintptr_t enemy, Slot& slot, const rule::MotionItem& item) {
    if (item.motionNo != pose_wire::kNoMotion && !pose_game::motionAvailable(enemy, pose_wire::kBodyTarget, item.motionNo)) {
        debug_stats::count(Counter::EnemyObserverMisses);
        logger::writeUnlessRepeated("enemy_observer: slot %zu has no motion 0x%04x loaded", index, item.motionNo);
        return;
    }
    if (!pose_game::replay(enemy, pose_wire::kBodyTarget, {item.layer, item.motionNo, item.flags, item.interp, item.start})) return;
    debug_stats::count(Counter::EnemyObserverMotions);
    if (item.layer < kFollowedLayers) slot.layerFlags[item.layer] = item.flags;
}

// One of the enemy's motion layers has fallen away from the simulator's (a request lost, or one that bypasses
// cAnimControl::set, as the overlay layer's hit reactions do): request the simulator's again at the simulator's frame.
// A dead enemy follows a change of motion only: re-seeking its fall on drift replayed it in steps.
void followLayer(size_t index, uintptr_t enemy, Slot& slot, size_t layer, const pose_wire::Layer& owner, bool alive) {
    puppet_rule::LayerFrame own;
    if (!pose_game::readLayerFrame(enemy, layer, own) || slot.seek[layer].update(own, owner, alive) != puppet_rule::Verdict::Seek) return;
    if (owner.motion != pose_wire::kNoMotion && !pose_game::motionAvailable(enemy, pose_wire::kBodyTarget, owner.motion)) {
        debug_stats::count(Counter::EnemyObserverMisses);
        logger::writeUnlessRepeated("enemy_observer: slot %zu has no motion 0x%04x loaded", index, owner.motion);
        return;
    }
    const auto request = pose_game::MotionRequest{static_cast<uint32_t>(layer), owner.motion, slot.layerFlags[layer], kNoSeekInterp, owner.frame};
    if (!pose_game::replay(enemy, pose_wire::kBodyTarget, request)) return;
    debug_stats::count(Counter::EnemyObserverSeeks);
    logger::writeUnlessRepeated("enemy_observer: slot %zu layer %zu sought motion 0x%04x frame %.1f (was 0x%04x frame %.1f)", index,
                                layer, owner.motion, owner.frame, own.motion, own.frame);
}

void announce(size_t index, Slot& slot) {
    if (slot.announced) return;
    slot.announced = true;
    logger::write("enemy_observer: slot %zu (%s) observed from slot %u e%u", index, enemy_policy::nameOf(slot.local.classIndex),
                  slot.source, g_world.epoch);
}

// Applies what the stream shows for this frame to the enemy, before its unit move. The enemy keeps its last state when
// the stream gives nothing; false when the simulator removed the unit (its own update runs from now on).
bool drive(size_t index, uintptr_t enemy, Slot& slot) {
    if (!slot.clock) {
        noteHold(index, slot, Hold::NoData);
        return true;
    }
    const rule::Shown shown = slot.timeline.render(*slot.clock, g_world.nowMs);
    if (shown.stale) {
        noteHold(index, slot, Hold::Stale);
        return true;
    }
    if (!shown.valid) {
        noteHold(index, slot, slot.bindingRefused ? Hold::Binding : Hold::NoData);
        return true;
    }
    noteHold(index, slot, Hold::None);
    announce(index, slot);
    if (shown.reset != rule::Reset::None) {
        logger::write("enemy_observer: slot %zu timeline reset (%s)", index, rule::name(shown.reset));
        for (puppet_rule::SeekTracker& tracker : slot.seek) tracker.requestSeek();
    }
    const rule::Life life = rule::lifeOf(shown.sample.flags);
    if (life == rule::Life::Gone) {
        slot.released = true;
        logger::write("enemy_observer: slot %zu removed", index);
        return false;
    }
    if (life == rule::Life::Dead && !slot.terminalLogged) {
        slot.terminalLogged = true;
        logger::write("enemy_observer: slot %zu terminal", index);
    }
    enemy_observer_game::write(enemy, shown.sample);
    slot.written = shown.sample;
    g_due.clear();
    slot.motions.takeDue(slot.targetMs, g_due);
    for (const rule::MotionItem& item : g_due) replayMotion(index, enemy, slot, item);
    const bool alive = life == rule::Life::Alive;
    followLayer(index, enemy, slot, 0, {shown.sample.motion0, shown.sample.frame0}, alive);
    followLayer(index, enemy, slot, 1, {shown.sample.motion1, shown.sample.frame1}, alive);
    return true;
}

// Index of the pool slot whose enemy is `unit`, or kNoIndex.
int poolIndexOf(uintptr_t unit) {
    const auto found = std::find(g_world.pool.begin(), g_world.pool.end(), unit);
    return found == g_world.pool.end() ? kNoIndex : static_cast<int>(found - g_world.pool.begin());
}

// Index of the pool slot whose engine update is replaced for `unit` now (an enemy a stream shows, past construction, or
// one that freeze() holds), or kNoIndex. The pool copy is last tick's: a unit the walk deleted and a later unit that took
// its address would match it, so the live pool must still hold `unit` in that slot (nothing is written into anything else).
int scopedSlot(uintptr_t unit) {
    const int index = poolIndexOf(unit);
    if (index == kNoIndex || enemy_registry::enemyAt(index) != unit) return kNoIndex;
    Slot& slot = g_slots[static_cast<size_t>(index)];
    if (slot.frozenEnemy == unit) return index;
    if (!g_slotsLive) return kNoIndex;
    int32_t state = rule::kSetupState;
    if (!slot.observer || !enemy_observer_game::recordState(unit, state)) return kNoIndex;
    slot.gate.note(state);
    if (rule::scoped(slot.observer, slot.gate.open(), slot.released)) return index;
    if (!slot.gate.open()) noteHold(static_cast<size_t>(index), slot, Hold::Construction);
    return kNoIndex;
}

// A unit whose update is not replaced: an enemy this machine simulates has the sounds of its own code sent (hit_fx).
void moveSimulatedOrOther(void* unit, void* edx) {
    const uintptr_t enemy = reinterpret_cast<uintptr_t>(unit);
    const int index = g_slotsLive ? poolIndexOf(enemy) : kNoIndex;
    const bool simulatedHere = index != kNoIndex && enemy_observer::simulatorOf(static_cast<uint8_t>(index)).here;
    if (simulatedHere) hit_fx::beginMove(enemy);
    g_originalUnitMove(unit, edx);
    if (simulatedHere) hit_fx::endMove();
}

// Slot 8 of every unit. A pass-through for players and for every enemy that is not observed or frozen; an observed
// enemy gets the stream's state and then runs its move with the CharPause bit held, restored as found, and is compared
// with what was written once the move is over (the write comes first, the check last); a frozen one runs its move with
// the bit held and nothing written.
void __fastcall unitMoveDetour(void* unit, void* edx) {
    const uintptr_t enemy = reinterpret_cast<uintptr_t>(unit);
    const int index = scopedSlot(enemy);
    if (index == kNoIndex) return moveSimulatedOrOther(unit, edx);
    Slot& slot = g_slots[static_cast<size_t>(index)];
    slot.written.reset();
    if (slot.frozenEnemy != enemy && !drive(static_cast<size_t>(index), enemy, slot)) return g_originalUnitMove(unit, edx);
    enemy_observer_game::CharPause held;
    enemy_observer_game::holdCharPause(held);
    g_originalUnitMove(unit, edx);
    enemy_observer_game::releaseCharPause(held);
    if (slot.written) enemy_drift::afterMove(static_cast<size_t>(index), enemy, slot.local.classIndex, *slot.written);
}

// ---- Game thread: once a frame -----------------------------------------------------------------------------------

void logOncePerSecond() {
    if (g_logStarted && stream_clock::after(g_nextLogMs, g_world.nowMs)) return;
    g_logStarted = true;
    g_nextLogMs = g_world.nowMs + kLogIntervalMs;
    const auto observed = static_cast<unsigned>(std::count_if(g_slots.begin(), g_slots.end(), [](const Slot& slot) {
        return slot.announced && !slot.released;
    }));
    if (observed == 0) return;
    const debug_stats::Snapshot stats = debug_stats::snapshot();
    const auto total = [&](Counter counter) { return stats.total[static_cast<size_t>(counter)]; };
    logger::write("enemy_observer: %u observed, poses %u/s, motions %u, misses %u, seeks %u, stale %u", observed,
                  stats.perSecond[static_cast<size_t>(Counter::EnemyObserverPoses)], total(Counter::EnemyObserverMotions),
                  total(Counter::EnemyObserverMisses), total(Counter::EnemyObserverSeeks), total(Counter::EnemyObserverStale));
}

void onTick() {
    refreshWorld();
    const bool leaving = g_departure.starts(game_state::doorActive(), g_world.simulating,
                                            door_travel::peerPlace() == door_travel::PeerPlace::Here);
    if (g_world.simulating || enemy_lease::anyHeld()) {
        enemy_observer_capture::tick(g_world.scene, g_world.epoch, g_world.simulating);
        if (leaving) enemy_observer_capture::sendFinal(g_world.nowMs);
    } else {
        enemy_observer_capture::stop();
    }
    g_slotsLive = g_world.observing || enemy_lease::anyAway();
    if (!g_slotsLive) return dropPending();
    refreshSlots();
    g_pacer.beginFrame();
    drainPoses();
    drainMotions();
    logOncePerSecond();
}

}  // namespace

namespace enemy_observer {

bool enable() {
    if (!frame_router::add(wire::kMsgEnemyPose, frame_router::SenderPolicy::Any, onFrame) ||
        !frame_router::add(wire::kMsgEnemyMotion, frame_router::SenderPolicy::Any, onFrame) ||
        !enemy_observer_capture::enable()) {
        return false;
    }
    if (!hooks::install("enemy unit move", game::kUnitMoveFunction, reinterpret_cast<void*>(&unitMoveDetour),
                        reinterpret_cast<void**>(&g_originalUnitMove))) {
        return false;
    }
    game_tick::addCallback("enemy_observer", onTick);
    g_enabled = true;
    return true;
}

void uninstall() {
    if (g_enabled.exchange(false)) hooks::remove(game::kUnitMoveFunction);
}

rule::Simulator simulatorOf(uint8_t slot) {
    if (slot >= game::kEnemyPoolSlots) return {};
    switch (enemy_lease::roleOf(slot)) {
        case enemy_lease::Role::Away: return {true, false, enemy_lease::peerOf(slot).value_or(0)};
        case enemy_lease::Role::Held: return {true, true, character_owner::localSlot()};
        case enemy_lease::Role::None: break;
    }
    const std::optional<room_authority::Holder> holder = room_authority::holderOf(scene::current());
    if (!holder) return {};
    return {true, holder->here, holder->slot};
}

std::optional<uint8_t> streamSlotOf(int slot) {
    if (slot < 0 || slot >= game::kEnemyPoolSlots || g_slots[static_cast<size_t>(slot)].streamSlot == kNoIndex) return std::nullopt;
    return static_cast<uint8_t>(g_slots[static_cast<size_t>(slot)].streamSlot);
}

int slotOfRecord(uint16_t record) { return slotOfRecordHere(record); }

bool toLocalSlot(uint8_t& slot, uint16_t record) {
    const int local = slotOfRecordHere(record);
    if (local == kNoIndex) return false;
    slot = static_cast<uint8_t>(local);
    return true;
}

bool freeze(int slot) {
    if (slot < 0 || slot >= game::kEnemyPoolSlots || !g_world.pool[static_cast<size_t>(slot)]) return false;
    g_slots[static_cast<size_t>(slot)].frozenEnemy = g_world.pool[static_cast<size_t>(slot)];
    return true;
}

std::optional<wire::PoseEntry> snapshot(int slot) {
    if (slot < 0 || slot >= game::kEnemyPoolSlots) return std::nullopt;
    wire::PoseEntry entry{};
    if (!enemy_observer_capture::entryFor(slot, g_world.pool[static_cast<size_t>(slot)], entry)) return std::nullopt;
    entry.flags = entry.hp > 0 ? wire::kFlagAlive : wire::kFlagTerminal;
    return entry;
}

bool startAt(int slot, const int32_t (&record)[4]) {
    if (slot < 0 || slot >= game::kEnemyPoolSlots) return false;
    const uintptr_t enemy = g_world.pool[static_cast<size_t>(slot)];
    wire::PoseEntry state{};
    if (!enemy || !enemy_observer_game::read(enemy, state)) return false;
    g_slots[static_cast<size_t>(slot)].frozenEnemy = 0;
    carryOn(enemy, state.hp, record);
    return true;
}

bool replicates(uintptr_t enemy) {
    const int index = poolIndexOf(enemy);
    if (index == kNoIndex) return false;
    const Slot& slot = g_slots[static_cast<size_t>(index)];
    return slot.role == enemy_lease::Role::Held || (g_slotsLive && slot.observer);
}

bool resume(int slot, const wire::PoseEntry& state) {
    if (slot < 0 || slot >= game::kEnemyPoolSlots) return false;
    const uintptr_t enemy = g_world.pool[static_cast<size_t>(slot)];
    if (!enemy) return false;
    float quat[4];
    pose_wire::unpackQuat(state.quat, quat);
    enemy_observer_game::place(enemy, state.pos, quat, state.hp);
    carryOn(enemy, state.hp, state.record);
    return true;
}

}  // namespace enemy_observer
