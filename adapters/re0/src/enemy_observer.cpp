#include "enemy_observer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <vector>

#include "debug_stats.h"
#include "enemy_observer_capture.h"
#include "enemy_observer_game.h"
#include "enemy_observer_rule.h"
#include "enemy_observer_wire.h"
#include "enemy_policy.h"
#include "enemy_registry.h"
#include "enemy_spawn.h"
#include "frame_router.h"
#include "game.h"
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
constexpr int kNoIndex = -1;
constexpr float kNoSeekInterp = 0.0f;  // a seek restarts the motion at the simulator's frame without blending

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
    uint32_t targetMs = 0;  // the simulator's time to show
    std::array<uintptr_t, game::kEnemyPoolSlots> pool{};
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
    bool observer = false;  // an Observer-class enemy whose spawn is known
    rule::LocalEnemy local;
    rule::ConstructionGate gate;
    bool released = false;         // the simulator removed the unit: its own code takes it out
    bool announced = false;
    bool terminalLogged = false;
    bool bindingRefused = false;   // a stream entry for this slot named another class or spawn
    Hold hold = Hold::None;
    uint32_t holdSinceMs = 0;
    rule::SlotTimeline timeline;
    timed_queue::TimedQueue<rule::MotionItem> motions;
    puppet_rule::SeekTracker seek;
    uint32_t layer0Flags = 0;  // flags of the last layer-0 request, reused by a seek
};
std::array<Slot, game::kEnemyPoolSlots> g_slots;
uint32_t g_motionSeq = 0;  // newest request number seen on the stream
bool g_motionSeqKnown = false;
uint32_t g_nextLogMs = 0;
bool g_logStarted = false;
std::vector<rule::MotionItem> g_due;

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
    dropPending();
}

void refreshWorld() {
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
    if (next.observing) {
        next.clock = puppet::clockFor(next.holderSlot);
        if (next.clock) next.targetMs = next.clock->target(next.nowMs);
    }
    for (int slot = 0; slot < game::kEnemyPoolSlots; ++slot) next.pool[static_cast<size_t>(slot)] = enemy_registry::enemyAt(slot);
    const bool sameStream = next.observing == g_world.observing && next.scene == g_world.scene &&
                            next.holderSlot == g_world.holderSlot && next.epoch == g_world.epoch;
    if (!sameStream) {
        resetStreams();
        if (next.observing) {
            logger::write("enemy_observer: showing scene 0x%02x from slot %u e%u", static_cast<unsigned>(next.scene), next.holderSlot, next.epoch);
        } else if (g_world.observing) {
            logger::write("enemy_observer: stopped showing scene 0x%02x", static_cast<unsigned>(g_world.scene));
        }
    }
    g_world = std::move(next);
}

// Each pool slot's enemy: the class and spawn it is bound by. A slot that holds another enemy starts over.
void refreshSlots() {
    for (size_t index = 0; index < g_slots.size(); ++index) {
        Slot& slot = g_slots[index];
        const uintptr_t enemy = g_world.pool[index];
        const int classIndex = enemy ? enemy_policy::classIndexOf(game::readPointer(enemy)) : enemy_policy::kNoClass;
        const std::optional<enemy_spawn::Origin> origin = enemy ? enemy_spawn::originOf(static_cast<int>(index), enemy) : std::nullopt;
        const bool observer = origin && classIndex != enemy_policy::kNoClass &&
                              rule::observed(enemy_policy::kClasses[static_cast<size_t>(classIndex)].policy, g_world.observing);
        const rule::LocalEnemy local = observer ? rule::LocalEnemy{static_cast<uint8_t>(classIndex), origin->spawnId} : rule::LocalEnemy{};
        if (slot.enemy == enemy && slot.observer == observer && slot.local == local) continue;
        slot = Slot{};
        slot.enemy = enemy;
        slot.observer = observer;
        slot.local = local;
    }
}

// ---- Game thread: draining the net thread's batches --------------------------------------------------------------

// False (and counted) when the batch is not from the slot that runs this room under the table's epoch.
bool accepted(const wire::Header& header, uint8_t sender) {
    const rule::Refusal refusal = rule::refusalOf(header, sender, g_world.holderSlot, g_world.epoch, g_world.scene);
    if (refusal == rule::Refusal::None) return true;
    debug_stats::count(Counter::EnemyObserverRefused);
    logger::writeUnlessRepeated("enemy_observer: batch from slot %u refused (%s)", sender, rule::name(refusal));
    return false;
}

void takePose(const wire::Header& header, const wire::PoseEntry& entry, uint32_t arrivalMs) {
    Slot& slot = g_slots[entry.slot];
    if (!slot.observer || !rule::bound(slot.local, entry)) {
        if (!slot.bindingRefused) {
            logger::write("enemy_observer: slot %u entry refused, stream %s/%u, here %s/%u", entry.slot,
                          enemy_policy::nameOf(entry.classIndex), entry.spawnId, enemy_policy::nameOf(slot.local.classIndex),
                          slot.local.spawnId);
        }
        slot.bindingRefused = true;
        debug_stats::count(Counter::EnemyObserverRefused);
        return;
    }
    slot.bindingRefused = false;
    slot.timeline.push(*g_world.clock, rule::sampleOf(header, entry, arrivalMs));
    debug_stats::count(Counter::EnemyObserverPoses);
}

void drainPoses() {
    std::vector<PoseArrival> batches;
    {
        std::lock_guard lock(g_mutex);
        batches.swap(g_poses);
    }
    for (const PoseArrival& arrival : batches) {
        if (!g_world.clock || !accepted(arrival.batch.header, arrival.sender)) continue;
        for (const wire::PoseEntry& entry : arrival.batch.entries) takePose(arrival.batch.header, entry, arrival.arrivalMs);
    }
}

void takeMotion(const wire::Header& header, const wire::MotionEntry& entry) {
    const uint32_t seq = g_motionSeqKnown ? rule::extendSeq(g_motionSeq, entry.seqLow) : entry.seqLow;
    if (!g_motionSeqKnown || stream_clock::after(seq, g_motionSeq)) g_motionSeq = seq;
    g_motionSeqKnown = true;
    Slot& slot = g_slots[entry.slot];
    if (slot.observer) slot.motions.push(rule::itemOf(header, entry, seq));
}

void drainMotions() {
    std::vector<MotionArrival> batches;
    {
        std::lock_guard lock(g_mutex);
        batches.swap(g_motions);
    }
    for (const MotionArrival& arrival : batches) {
        if (!accepted(arrival.batch.header, arrival.sender)) continue;
        for (const wire::MotionEntry& entry : arrival.batch.entries) takeMotion(arrival.batch.header, entry);
    }
}

// ---- Game thread: one enemy's unit move --------------------------------------------------------------------------

// Runs one simulator request on the enemy; skipped and counted when its motion banks lack the motion.
void replayMotion(size_t index, uintptr_t enemy, Slot& slot, const rule::MotionItem& item) {
    if (item.motionNo != pose_wire::kNoMotion && !pose_game::motionAvailable(enemy, item.motionNo)) {
        debug_stats::count(Counter::EnemyObserverMisses);
        logger::writeUnlessRepeated("enemy_observer: slot %zu has no motion 0x%04x loaded", index, item.motionNo);
        return;
    }
    if (!pose_game::replay(enemy, {item.layer, item.motionNo, item.flags, item.interp, item.start})) return;
    debug_stats::count(Counter::EnemyObserverMotions);
    if (item.layer == 0) slot.layer0Flags = item.flags;
}

// The enemy's body motion has fallen away from the simulator's (a request lost, or one that bypasses cAnimControl::set):
// request the simulator's again at the simulator's frame.
void followBody(size_t index, uintptr_t enemy, Slot& slot, const rule::Sample& shown) {
    puppet_rule::Layer0 own;
    const pose_wire::Layer owner{shown.motion0, shown.frame0};
    if (!pose_game::readLayer0(enemy, own) || slot.seek.update(own, owner) != puppet_rule::Verdict::Seek) return;
    if (owner.motion != pose_wire::kNoMotion && !pose_game::motionAvailable(enemy, owner.motion)) {
        debug_stats::count(Counter::EnemyObserverMisses);
        logger::writeUnlessRepeated("enemy_observer: slot %zu has no motion 0x%04x loaded", index, owner.motion);
        return;
    }
    if (pose_game::replay(enemy, {0, owner.motion, slot.layer0Flags, kNoSeekInterp, owner.frame})) {
        debug_stats::count(Counter::EnemyObserverSeeks);
    }
}

void announce(size_t index, Slot& slot) {
    if (slot.announced) return;
    slot.announced = true;
    logger::write("enemy_observer: slot %zu (%s) observed from slot %u e%u", index, enemy_policy::nameOf(slot.local.classIndex),
                  g_world.holderSlot, g_world.epoch);
}

// Applies what the stream shows for this frame to the enemy, before its unit move. The enemy keeps its last state when
// the stream gives nothing; false when the simulator removed the unit (its own update runs from now on).
bool drive(size_t index, uintptr_t enemy, Slot& slot) {
    if (!g_world.clock) {
        noteHold(index, slot, Hold::NoData);
        return true;
    }
    const rule::Shown shown = slot.timeline.render(*g_world.clock, g_world.nowMs);
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
        slot.seek.requestSeek();
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
    g_due.clear();
    slot.motions.takeDue(g_world.targetMs, g_due);
    for (const rule::MotionItem& item : g_due) replayMotion(index, enemy, slot, item);
    followBody(index, enemy, slot, shown.sample);
    return true;
}

// Index of the pool slot whose enemy is `unit`, or kNoIndex.
int poolIndexOf(uintptr_t unit) {
    const auto found = std::find(g_world.pool.begin(), g_world.pool.end(), unit);
    return found == g_world.pool.end() ? kNoIndex : static_cast<int>(found - g_world.pool.begin());
}

// Index of the pool slot whose observed enemy is `unit` and whose engine update is replaced now, or kNoIndex.
int scopedSlot(uintptr_t unit) {
    if (!g_world.observing) return kNoIndex;
    const int index = poolIndexOf(unit);
    if (index == kNoIndex) return kNoIndex;
    Slot& slot = g_slots[static_cast<size_t>(index)];
    int32_t state = rule::kSetupState;
    if (!slot.observer || !enemy_observer_game::recordState(unit, state)) return kNoIndex;
    slot.gate.note(state);
    if (rule::scoped(slot.observer, slot.gate.open(), slot.released)) return index;
    if (!slot.gate.open()) noteHold(static_cast<size_t>(index), slot, Hold::Construction);
    return kNoIndex;
}

// Slot 8 of every unit. A pass-through for players and for every enemy that is not observed; an observed enemy gets the
// stream's state and then runs its move with the CharPause bit held, restored as found.
void __fastcall unitMoveDetour(void* unit, void* edx) {
    const uintptr_t enemy = reinterpret_cast<uintptr_t>(unit);
    const int index = scopedSlot(enemy);
    if (index == kNoIndex) return g_originalUnitMove(unit, edx);
    if (!drive(static_cast<size_t>(index), enemy, g_slots[static_cast<size_t>(index)])) return g_originalUnitMove(unit, edx);
    enemy_observer_game::CharPause held;
    enemy_observer_game::holdCharPause(held);
    g_originalUnitMove(unit, edx);
    enemy_observer_game::releaseCharPause(held);
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
    if (g_world.simulating) {
        enemy_observer_capture::tick(g_world.scene, g_world.epoch);
    } else {
        enemy_observer_capture::stop();
    }
    if (!g_world.observing) return dropPending();
    refreshSlots();
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

bool observes(uintptr_t enemy) {
    if (!g_world.observing) return false;
    const int index = poolIndexOf(enemy);
    return index != kNoIndex && g_slots[static_cast<size_t>(index)].observer;
}

}  // namespace enemy_observer
