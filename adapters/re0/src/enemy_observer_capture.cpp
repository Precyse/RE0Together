#include "enemy_observer_capture.h"

#include <algorithm>
#include <array>
#include <vector>

#include "debug_stats.h"
#include "enemy_observer_game.h"
#include "enemy_observer_send_rule.h"
#include "enemy_observer_wire.h"
#include "enemy_policy.h"
#include "enemy_registry.h"
#include "enemy_spawn.h"
#include "enemy_target.h"
#include "game.h"
#include "log.h"
#include "pose_capture_rule.h"
#include "pose_game.h"
#include "pose_wire.h"
#include "state_net.h"
#include "stream_clock.h"

namespace {

namespace send_rule = enemy_observer_send_rule;
namespace wire = enemy_observer_wire;
using debug_stats::Counter;

constexpr uint32_t kLogIntervalMs = 1000;
constexpr size_t kMaxPendingMotions = 512;  // requests of one frame; far above anything a frame makes
constexpr uint32_t kSeqLowMask = 0xffff;
constexpr uint32_t kMaxDtMs = 0xffff;

struct PendingMotion {
    uint32_t requestMs;
    wire::MotionEntry entry;
};

bool g_capturing = false;  // game thread only, like everything below
uint16_t g_scene = 0;
uint32_t g_epoch = 0;
uint32_t g_batchSeq = 0;   // one number per batch of either type
uint32_t g_motionSeq = 0;  // one number per motion request
std::vector<PendingMotion> g_pending;
send_rule::SendPlanner g_planner;
pose_capture_rule::SampleTimer g_timer{send_rule::kPoseIntervalMs};
std::array<wire::PoseEntry, game::kEnemyPoolSlots> g_last{};  // the last entry built per slot: a removed enemy is announced with it
uint32_t g_enemiesSent = 0;  // since the last log line
uint32_t g_bytesSent = 0;
uint32_t g_nextLogMs = 0;
bool g_logStarted = false;

void onMotionRequest(uintptr_t animControl, const pose_game::MotionRequest& request) {
    if (!g_capturing || request.layer >= pose_wire::kMotionLayers) return;
    const uintptr_t enemy = animControl - game::kAnimControlOffset;
    if (enemy_policy::policyOf(game::readPointer(enemy)) != enemy_policy::Policy::Observer) return;
    const int slot = enemy_registry::slotOf(enemy);
    if (slot == enemy_registry::kNoSlot || g_pending.size() >= kMaxPendingMotions) return;
    g_pending.push_back({pose_wire::clockMs(),
                         {static_cast<uint8_t>(slot), static_cast<uint8_t>(request.layer), static_cast<uint16_t>(request.motionNo),
                          request.flags, request.interp, request.start, 0, 0}});
}

wire::Header headerAt(uint32_t nowMs) { return {nowMs, 0, g_epoch, g_scene, 0, 0}; }

template <typename Entry>
void sendBatches(uint16_t type, bool reliable, uint32_t nowMs, const std::vector<Entry>& entries) {
    if (entries.empty()) return;
    for (const std::vector<uint8_t>& payload : wire::encode<Entry>(headerAt(nowMs), entries, g_batchSeq)) {
        if (state_net::send(type, reliable, payload)) g_bytesSent += static_cast<uint32_t>(payload.size());
    }
}

// The frame's motion requests, in one reliable batch (more than kMaxEntries split).
void flushMotions(uint32_t nowMs) {
    if (g_pending.empty()) return;
    std::vector<wire::MotionEntry> entries;
    entries.reserve(g_pending.size());
    for (const PendingMotion& pending : g_pending) {
        wire::MotionEntry entry = pending.entry;
        entry.dtMs = static_cast<uint16_t>(std::min(nowMs - pending.requestMs, kMaxDtMs));
        entry.seqLow = static_cast<uint16_t>(++g_motionSeq & kSeqLowMask);
        entries.push_back(entry);
    }
    g_pending.clear();
    sendBatches<wire::MotionEntry>(wire::kMsgEnemyMotion, true, nowMs, entries);
}

// One pose round over the pool: living enemies and repeated corpses go unreliably, deaths and removals once, reliably.
void sendRound(uint32_t nowMs) {
    std::vector<wire::PoseEntry> unreliable;
    std::vector<wire::PoseEntry> reliable;
    g_planner.startRound();
    for (int slot = 0; slot < game::kEnemyPoolSlots; ++slot) {
        const uintptr_t enemy = enemy_registry::enemyAt(slot);
        wire::PoseEntry entry{};
        const bool present = enemy_observer_capture::entryFor(slot, enemy, entry);
        const bool alive = present && entry.hp > 0;
        const send_rule::Action action = g_planner.next(static_cast<uint8_t>(slot), present, alive, static_cast<uint32_t>(enemy));
        wire::PoseEntry& last = g_last[static_cast<size_t>(slot)];
        switch (action) {
            case send_rule::Action::Pose:
                entry.flags = wire::kFlagAlive;
                last = entry;
                unreliable.push_back(entry);
                break;
            case send_rule::Action::Corpse:
                entry.flags = wire::kFlagTerminal;
                last = entry;
                unreliable.push_back(entry);
                break;
            case send_rule::Action::Terminal:
                entry.flags = wire::kFlagTerminal;
                last = entry;
                reliable.push_back(entry);
                logger::write("enemy_observer: slot %d terminal", slot);
                break;
            case send_rule::Action::Removed:
                last.flags = wire::kFlagRemoved;
                reliable.push_back(last);
                logger::write("enemy_observer: slot %d removed", slot);
                break;
            case send_rule::Action::Skip:
                break;
        }
    }
    g_enemiesSent += static_cast<uint32_t>(unreliable.size());
    debug_stats::count(Counter::EnemyObserverSent, static_cast<uint32_t>(unreliable.size() + reliable.size()));
    sendBatches<wire::PoseEntry>(wire::kMsgEnemyPose, false, nowMs, unreliable);
    sendBatches<wire::PoseEntry>(wire::kMsgEnemyPose, true, nowMs, reliable);
}

void logOncePerSecond(uint32_t nowMs) {
    if (g_logStarted && stream_clock::after(g_nextLogMs, nowMs)) return;
    g_logStarted = true;
    g_nextLogMs = nowMs + kLogIntervalMs;
    if (g_bytesSent > 0) {
        logger::write("enemy_observer: sending %u enemies, %u B/s", g_enemiesSent / send_rule::kRoundsPerSecond, g_bytesSent);
    }
    g_enemiesSent = 0;
    g_bytesSent = 0;
}

}  // namespace

namespace enemy_observer_capture {

bool entryFor(int slot, uintptr_t enemy, wire::PoseEntry& entry) {
    const std::optional<enemy_spawn::Origin> origin = enemy ? enemy_spawn::originOf(slot, enemy) : std::nullopt;
    const int classIndex = enemy ? enemy_policy::classIndexOf(game::readPointer(enemy)) : enemy_policy::kNoClass;
    if (!origin || classIndex == enemy_policy::kNoClass ||
        enemy_policy::kClasses[static_cast<size_t>(classIndex)].policy != enemy_policy::Policy::Observer) {
        return false;
    }
    entry = {};
    if (!enemy_observer_game::read(enemy, entry)) return false;
    entry.slot = static_cast<uint8_t>(slot);
    entry.classIndex = static_cast<uint8_t>(classIndex);
    entry.target = enemy_target::read(enemy);
    entry.spawnId = origin->spawnId;
    entry.spawnRecord = origin->record;
    return true;
}

bool enable() { return pose_game::addAnimObserver(onMotionRequest); }

void tick(uint16_t scene, uint32_t epoch) {
    g_capturing = true;
    g_scene = scene;
    g_epoch = epoch;
    const uint32_t now = pose_wire::clockMs();
    flushMotions(now);
    if (g_timer.due(now)) sendRound(now);
    logOncePerSecond(now);
}

void stop() {
    if (!g_capturing) return;
    g_capturing = false;
    g_pending.clear();
    g_planner.clear();
    g_timer = pose_capture_rule::SampleTimer{send_rule::kPoseIntervalMs};
    g_logStarted = false;
    g_enemiesSent = 0;
    g_bytesSent = 0;
    logger::write("enemy_observer: stopped sending");
}

}  // namespace enemy_observer_capture
