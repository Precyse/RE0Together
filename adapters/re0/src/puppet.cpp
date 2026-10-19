#include "puppet.h"

#include <array>
#include <atomic>
#include <mutex>
#include <vector>

#include "character_owner.h"
#include "debug_stats.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "hooks.h"
#include "log.h"
#include "net_pad.h"
#include "pose_game.h"
#include "pose_timeline.h"
#include "pose_wire.h"
#include "puppet_rule.h"
#include "room_phase.h"
#include "scene.h"

namespace {

using character_owner::Character;
using debug_stats::Counter;

constexpr uint32_t kLogIntervalMs = 1000;
constexpr int kNoSlot = -1;
constexpr float kNoSeekInterp = 0.0f;  // a seek restarts the motion at the owner's frame without blending

using DamageEntryFunction = uint32_t(__fastcall*)(void* self, void* edx, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
DamageEntryFunction g_originalDamageEntry = nullptr;

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_hasPose{false};  // a pose from the driving peer arrived

std::mutex g_mutex;
pose_timeline::Timeline g_timeline;  // guarded by g_mutex
int g_sourceSlot = kNoSlot;          // guarded by g_mutex

// Game thread only.
std::array<bool, character_owner::kCharacterCount> g_puppeted{};
std::vector<pose_wire::MotionEvent> g_due;
puppet_rule::SeekTracker g_seek;
uint32_t g_layer0Flags = 0;  // flags of the last layer-0 request, reused by a seek
uintptr_t g_scopePlayer = 0;
uint32_t g_flagsBefore = 0;
uint32_t g_flagsDuring = 0;
uint32_t g_flagsAfter = 0;
uint32_t g_nextLogMs = 0;
bool g_logStarted = false;

void discardStream() {
    std::lock_guard lock(g_mutex);
    g_timeline.clear();
    g_sourceSlot = kNoSlot;
    g_hasPose = false;
}

bool available(uintptr_t player, uint16_t motionNo) {
    return motionNo == pose_wire::kNoMotion || pose_game::motionAvailable(player, motionNo);
}

void countMiss(Character character, uint16_t motionNo) {
    debug_stats::count(Counter::PuppetMotionMisses);
    logger::writeUnlessRepeated("puppet: %s has no motion 0x%04x loaded", character_owner::name(character), motionNo);
}

// Runs one owner request on the puppet; skipped and counted when the puppet's banks lack the motion.
void replayEvent(uintptr_t player, Character character, const pose_wire::MotionEvent& event) {
    const pose_game::MotionRequest request{event.layer, event.motionNo, puppet_rule::replayFlags(event.layer, event.flags),
                                           event.interp, event.start};
    if (!available(player, event.motionNo)) {
        countMiss(character, event.motionNo);
    } else if (pose_game::replay(player, request) && event.layer == 0) {
        g_layer0Flags = request.flags;
    }
}

// The puppet's body motion has fallen away from the owner's: request the owner's again at the owner's frame.
void followLayer0(uintptr_t player, Character character, const pose_wire::Pose& pose) {
    puppet_rule::Layer0 own;
    if (!pose_game::readLayer0(player, own) || g_seek.update(own, pose.layer[0]) != puppet_rule::Verdict::Seek) return;
    const pose_wire::Layer& owner = pose.layer[0];
    if (!available(player, owner.motion)) {
        countMiss(character, owner.motion);
        return;
    }
    if (pose_game::replay(player, {0, owner.motion, g_layer0Flags, kNoSeekInterp, owner.frame})) {
        debug_stats::count(Counter::PuppetSeeks);
    }
}

// Once a second: what the puppet shows against what the owner sent, and the pause bit around its move.
void logState(uintptr_t player, Character character, const pose_wire::Pose& pose) {
    const uint32_t now = pose_wire::clockMs();
    if (g_logStarted && pose_timeline::after(g_nextLogMs, now)) return;
    g_logStarted = true;
    g_nextLogMs = now + kLogIntervalMs;
    puppet_rule::Layer0 own{};
    pose_game::readLayer0(player, own);
    pose_wire::Pose think;
    pose_game::readThink(player, think);
    const debug_stats::Snapshot stats = debug_stats::snapshot();
    logger::write(
        "puppet: %s motion 0x%04x (owner 0x%04x) frame %.1f (owner %.1f) think %u/%u/%u (owner %u/%u/%u) "
        "mode flags %08x -> %08x -> %08x, events in %u, misses %u, seeks %u",
        character_owner::name(character), own.motion, pose.layer[0].motion, own.frame, pose.layer[0].frame, think.group,
        think.state, think.phase, pose.group, pose.state, pose.phase, g_flagsBefore, g_flagsDuring, g_flagsAfter,
        stats.total[static_cast<size_t>(Counter::MotionEventsReceived)],
        stats.total[static_cast<size_t>(Counter::PuppetMotionMisses)], stats.total[static_cast<size_t>(Counter::PuppetSeeks)]);
}

// A door, a cutscene or another room: the engine or the room scripts move the character, the stream waits.
bool suspended(uintptr_t player, const pose_wire::Pose& pose) {
    return room_phase::isEvent(game_state::roomPhase()) || game_state::doorActive() || pose.room != scene::current() ||
           !game_state::inCurrentRoom(player);
}

void drive(uintptr_t player, Character character) {
    pose_wire::Pose pose;
    pose_timeline::Result result;
    g_due.clear();
    {
        std::lock_guard lock(g_mutex);
        result = g_timeline.render(pose_wire::clockMs(), pose, g_due);
    }
    if (!result.valid || suspended(player, pose)) return;
    if (result.staleDropped > 0) logger::writeUnlessRepeated("puppet: %u stale motion events dropped", result.staleDropped);
    pose_game::writePose(player, pose);
    for (const pose_wire::MotionEvent& event : g_due) replayEvent(player, character, event);
    followLayer0(player, character, pose);
    logState(player, character, pose);
}

void release(uintptr_t player, Character character) {
    const bool reset = pose_game::resetPlayer(player);
    g_seek.reset();
    g_layer0Flags = 0;
    logger::write("puppet: %s released%s", character_owner::name(character), reset ? "" : " (reset faulted)");
}

void enterMove(uintptr_t player) {
    if (!net_pad::active() && g_hasPose.load()) discardStream();
    const Character character = character_owner::identify(player);
    if (character == Character::Unknown) return;
    bool& was = g_puppeted[static_cast<size_t>(character)];
    const bool now = puppet::active(player);
    if (was && !now) release(player, character);
    if (now && !was) logger::write("puppet: %s is shown from the owner's pose", character_owner::name(character));
    was = now;
    if (!now) return;
    drive(player, character);
    g_flagsBefore = pose_game::modeFlags();
    pose_game::holdPause();
    g_flagsDuring = pose_game::modeFlags();
    g_scopePlayer = player;
}

void exitMove(uintptr_t player) {
    if (player != g_scopePlayer) return;
    pose_game::releasePause();
    g_flagsAfter = pose_game::modeFlags();
    g_scopePlayer = 0;
}

void onMoveScope(uintptr_t player, bool entering) {
    if (entering) {
        enterMove(player);
    } else {
        exitMove(player);
    }
}

// The engine's damage and grab entry refuses while a script pauses the character; a puppet is paused only during its
// own move, so enemies updating outside it are refused here.
uint32_t __fastcall damageEntryDetour(void* self, void* edx, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    if (puppet::active(reinterpret_cast<uintptr_t>(self))) return 0;
    return g_originalDamageEntry(self, edx, a, b, c, d, e);
}

}  // namespace

namespace puppet {

bool enable() {
    if (!hooks::install("player damage entry", game::kPlayerDamageEntryFunction, reinterpret_cast<void*>(&damageEntryDetour),
                        reinterpret_cast<void**>(&g_originalDamageEntry))) {
        return false;
    }
    game_tick::addMoveScope(onMoveScope);
    g_enabled = true;
    return true;
}

void uninstall() {
    if (g_enabled.exchange(false)) hooks::remove(game::kPlayerDamageEntryFunction);
}

bool active(uintptr_t player) {
    if (!g_enabled.load(std::memory_order_relaxed)) return false;
    const Character character = character_owner::identify(player);
    return character != Character::Unknown && g_hasPose.load() &&
           character_owner::controlOf(character) == character_owner::Control::Remote;
}

void onFrame(const GameFrame& frame) {
    if (!g_enabled || static_cast<int>(frame.slot) != net_pad::peerSlot()) return;
    const uint32_t arrival = pose_wire::clockMs();
    std::lock_guard lock(g_mutex);
    if (static_cast<int>(frame.slot) != g_sourceSlot) {
        g_timeline.clear();
        g_sourceSlot = frame.slot;
    }
    if (frame.type == pose_wire::kMsgPoseState) {
        const auto pose = pose_wire::decodePose(frame.payload);
        if (!pose) return;
        g_timeline.addPose(*pose, arrival);
        g_hasPose = true;
        debug_stats::count(Counter::PosesReceived);
    } else if (const auto event = pose_wire::decodeEvent(frame.payload)) {
        g_timeline.addEvent(*event, arrival);
        debug_stats::count(Counter::MotionEventsReceived);
    }
}

}  // namespace puppet
