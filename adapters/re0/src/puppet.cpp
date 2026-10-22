#include "puppet.h"

#include <array>
#include <atomic>
#include <mutex>
#include <vector>

#include "character_owner.h"
#include "debug_stats.h"
#include "equip_refresh.h"
#include "equip_rule.h"
#include "equip_steps.h"
#include "frame_router.h"
#include "game_state.h"
#include "game_tick.h"
#include "log.h"
#include "net_pad.h"
#include "partner_think.h"
#include "player_contact.h"
#include "pose_game.h"
#include "pose_timeline.h"
#include "pose_wire.h"
#include "puppet_hooks.h"
#include "puppet_rule.h"
#include "room_phase.h"
#include "scene.h"
#include "stream_clock.h"

namespace {

using character_owner::Character;
using debug_stats::Counter;

constexpr uint32_t kLogIntervalMs = 1000;
constexpr int kNoSlot = -1;
constexpr size_t kMaxPeerSlots = 8;
constexpr float kNoSeekInterp = 0.0f;  // a seek restarts the motion at the owner's frame without blending

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_hasPose{false};  // a pose from the driving peer arrived
std::atomic<uint16_t> g_ownerRoom{scene::kNone};  // the room named by the owner's newest pose

std::mutex g_mutex;
pose_timeline::Timeline g_timeline;  // guarded by g_mutex
int g_sourceSlot = kNoSlot;          // guarded by g_mutex
uint32_t g_ownerRoomSeq = 0;         // guarded by g_mutex: the sequence of the pose g_ownerRoom came from
peer_sight::Sight g_latestSight{};   // guarded by g_mutex: the newest pose (by sequence); valid while g_ownerRoom is not scene::kNone
std::array<stream_clock::StreamClock, kMaxPeerSlots> g_clocks;  // one per peer slot, guarded by g_mutex

// Game thread only.
std::array<bool, character_owner::kCharacterCount> g_puppeted{};
std::vector<pose_wire::MotionEvent> g_due;
puppet_rule::SeekTracker g_seek;
puppet_rule::WeaponWatch g_weaponWatch;
uint32_t g_layer0Flags = 0;  // flags of the last layer-0 request, reused by a seek
uintptr_t g_scopePlayer = 0;
uintptr_t g_settlePlayer = 0;  // the puppet whose pose was written this move: it gets the post-move steps
uint32_t g_flagsBefore = 0;
uint32_t g_flagsDuring = 0;
uint32_t g_flagsAfter = 0;
uint32_t g_nextLogMs = 0;
bool g_logStarted = false;
bool g_stale = false;
uint32_t g_staleSinceMs = 0;
int32_t g_loggedEventPhase = room_phase::kUnreadable;

void discardStream() {
    std::lock_guard lock(g_mutex);
    g_timeline.clear();
    if (g_sourceSlot != kNoSlot) g_clocks[static_cast<size_t>(g_sourceSlot)].clear();
    g_sourceSlot = kNoSlot;
    g_hasPose = false;
    g_ownerRoom = scene::kNone;
}

bool available(uintptr_t player, uint16_t target, uint16_t motionNo) {
    return motionNo == pose_wire::kNoMotion || pose_game::motionAvailable(player, target, motionNo);
}

void countMiss(uintptr_t player, Character character, uint16_t target, uint16_t motionNo) {
    debug_stats::count(Counter::PuppetMotionMisses);
    if (pose_game::hasUnit(player, target)) {
        logger::writeUnlessRepeated("puppet: %s has no motion 0x%04x loaded", character_owner::name(character), motionNo);
    } else {
        logger::writeUnlessRepeated("puppet: %s sub-control %u has no unit", character_owner::name(character), target - 1u);
    }
}

// Runs one request on the puppet's anim control `target`, followed by the hand pose where the engine refreshes it;
// skipped and counted when the puppet's banks lack the motion. True when it ran.
bool replayRequest(uintptr_t player, Character character, uint16_t target, const pose_game::MotionRequest& request) {
    const uint16_t motionNo = static_cast<uint16_t>(request.motionNo);
    if (!available(player, target, motionNo)) {
        countMiss(player, character, target, motionNo);
        return false;
    }
    if (!pose_game::replay(player, target, request)) return false;
    if (puppet_rule::needsHandPose(request.layer, target)) pose_game::refreshHandPose(player);
    return true;
}

// Runs one owner request on the puppet.
void replayEvent(uintptr_t player, Character character, const pose_wire::MotionEvent& event) {
    const uint32_t layer = pose_wire::layerOf(event.layer);
    const uint16_t target = pose_wire::targetOf(event.layer);
    const pose_game::MotionRequest request{layer, event.motionNo, puppet_rule::replayFlags(layer, event.flags), event.interp,
                                           event.start};
    if (!replayRequest(player, character, target, request)) return;
    if (target != pose_wire::kBodyTarget) {
        debug_stats::count(Counter::PuppetSubEvents);
    } else if (layer == 0) {
        g_layer0Flags = request.flags;
    }
}

// The puppet's body motion has fallen away from the owner's: request the owner's again at the owner's frame.
void followLayer0(uintptr_t player, Character character, const pose_wire::Pose& pose) {
    puppet_rule::Layer0 own;
    if (!pose_game::readLayer0(player, own) || g_seek.update(own, pose.layer[0]) != puppet_rule::Verdict::Seek) return;
    const pose_wire::Layer& owner = pose.layer[0];
    if (replayRequest(player, character, pose_wire::kBodyTarget, {0, owner.motion, g_layer0Flags, kNoSeekInterp, owner.frame})) {
        debug_stats::count(Counter::PuppetSeeks);
    }
}

// The weapon the puppet holds is not the owner's and nothing is fixing it: re-equip from the mirrored inventory block.
void checkWeapon(uintptr_t player, Character character, const pose_wire::Pose& pose) {
    const int32_t held = equip_steps::heldType(player);
    if (held == equip_rule::kNoWeapon) return;
    if (!g_weaponWatch.update(pose.weapon, held, pose_wire::clockMs(), equip_refresh::pending(character)) ||
        !equip_refresh::requestFromMirror(character)) {
        return;
    }
    logger::write("puppet: %s weapon %02x (owner %02x), refreshing", character_owner::name(character),
                  static_cast<unsigned>(held), pose.weapon);
}

// Once a second: what the puppet shows against what the owner sent, and the pause bit around its move.
void logState(uintptr_t player, Character character, const pose_wire::Pose& pose, const pose_timeline::Result& shown) {
    const uint32_t now = pose_wire::clockMs();
    if (g_logStarted && stream_clock::after(g_nextLogMs, now)) return;
    g_logStarted = true;
    g_nextLogMs = now + kLogIntervalMs;
    puppet_rule::Layer0 own{};
    pose_game::readLayer0(player, own);
    pose_wire::Pose body;
    pose_game::readPose(player, body);
    const debug_stats::Snapshot stats = debug_stats::snapshot();
    logger::write(
        "puppet: %s motion 0x%04x (owner 0x%04x) frame %.1f (owner %.1f) think %u/%u/%u (owner %u/%u/%u) "
        "mode flags %08x -> %08x -> %08x, events in %u, misses %u, seeks %u, delay %dms, poses buffered %zu, "
        "status %08x/%08x hit %03x/%03x x1034 %x/%x pitch %.2f/%.2f sub %u",
        character_owner::name(character), own.motion, pose.layer[0].motion, own.frame, pose.layer[0].frame, body.group,
        body.state, body.phase, pose.group, pose.state, pose.phase, g_flagsBefore, g_flagsDuring, g_flagsAfter,
        stats.total[static_cast<size_t>(Counter::MotionEventsReceived)],
        stats.total[static_cast<size_t>(Counter::PuppetMotionMisses)], stats.total[static_cast<size_t>(Counter::PuppetSeeks)],
        shown.behindMs, shown.buffered, body.status, pose.status, body.hitAttr, pose.hitAttr, body.x1034, pose.x1034,
        body.manualPitch, pose.manualPitch, stats.total[static_cast<size_t>(Counter::PuppetSubEvents)]);
}

// A door or another room: the engine moves the character, the stream waits. Scripted events and cutscenes do not
// suspend it: the engine already holds both characters' thinks there, the owner's character runs the same scripts and
// its samples keep coming, and the thread's Body ops skip the puppet (event_body), so the stream is its only writer. A
// demo plays in its own room record, where the puppet is not in the loaded room and the other tests hold it.
bool suspended(uintptr_t player, const pose_wire::Pose& pose) {
    return game_state::doorActive() || pose.room != scene::current() || !game_state::inCurrentRoom(player);
}

// Says once per event phase that the stream drives the puppet through it.
void noteEventPhase(Character character) {
    const int32_t phase = game_state::roomPhase();
    if (!room_phase::isEvent(phase)) {
        g_loggedEventPhase = room_phase::kUnreadable;
        return;
    }
    if (phase == g_loggedEventPhase) return;
    g_loggedEventPhase = phase;
    logger::write("puppet: %s driven through %s phase", character_owner::name(character), room_phase::name(phase));
}

// The freshness of the owner's stream: the puppet holds while it is lost and says so once.
void trackFreshness(Character character, bool stale) {
    const uint32_t now = pose_wire::clockMs();
    if (stale && !g_stale) {
        g_stale = true;
        g_staleSinceMs = now;
        logger::write("puppet: %s stale, holding", character_owner::name(character));
    } else if (!stale && g_stale) {
        g_stale = false;
        logger::write("puppet: %s fresh again after %u ms", character_owner::name(character), now - g_staleSinceMs);
    }
}

void noteReset(Character character, pose_timeline::Reset reason) {
    logger::write("puppet: %s timeline reset (%s)", character_owner::name(character), pose_timeline::name(reason));
    g_seek.requestSeek();
}

// Everything that happens to the puppet before its move: the owner's shown pose, the owner's requests, the steps the
// paused move skips. False when nothing was written (no stream yet, lost, or the engine moves the character itself).
bool drive(uintptr_t player, Character character) {
    pose_wire::Pose pose;
    pose_timeline::Result result;
    g_due.clear();
    {
        std::lock_guard lock(g_mutex);
        if (g_sourceSlot == kNoSlot) return false;
        result = g_timeline.render(g_clocks[static_cast<size_t>(g_sourceSlot)], pose_wire::clockMs(), pose, g_due);
    }
    trackFreshness(character, result.stale);
    if (!result.valid) return false;
    if (result.reset != pose_timeline::Reset::None) noteReset(character, result.reset);
    if (suspended(player, pose)) return false;
    noteEventPhase(character);
    if (result.staleDropped > 0) logger::writeUnlessRepeated("puppet: %u stale motion events dropped", result.staleDropped);
    pose_game::writePose(player, pose);
    checkWeapon(player, character, pose);
    for (const pose_wire::MotionEvent& event : g_due) replayEvent(player, character, event);
    followLayer0(player, character, pose);
    pose_game::probeLight(player);
    logState(player, character, pose, result);
    return true;
}

void release(uintptr_t player, Character character) {
    const bool reset = pose_game::resetPlayer(player);
    g_seek.reset();
    g_weaponWatch.reset();
    g_layer0Flags = 0;
    g_stale = false;
    logger::write("puppet: %s released%s", character_owner::name(character), reset ? "" : " (reset faulted)");
    if (partner_think::restorePartnerAi(player)) logger::write("puppet: %s back to partner AI", character_owner::name(character));
}

// A partner AI think reads the local pad and decides; the puppet gets a controller think instead, once.
void ensureControllerThink(uintptr_t player, Character character) {
    if (partner_think::giveControllerThink(player)) {
        logger::write("puppet: %s given a controller think", character_owner::name(character));
    }
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
    ensureControllerThink(player, character);
    g_settlePlayer = drive(player, character) ? player : 0;
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

// What the paused branch of the move skips after the animation: the frame-driven hand swaps and the area id.
void afterMove(uintptr_t player) {
    if (player != g_settlePlayer) return;
    g_settlePlayer = 0;
    pose_game::settleAfterMove(player);
}

}  // namespace

namespace puppet {

bool enable() {
    if (!frame_router::add(pose_wire::kMsgPoseState, frame_router::SenderPolicy::Any, onFrame) ||
        !frame_router::add(pose_wire::kMsgMotionEvent, frame_router::SenderPolicy::Any, onFrame)) {
        return false;
    }
    if (!player_contact::enable()) return false;
    if (!puppet_hooks::enable()) {
        player_contact::uninstall();
        return false;
    }
    game_tick::addMoveScope(onMoveScope, "puppet");
    game_tick::addPostMove("puppet", afterMove);
    g_enabled = true;
    return true;
}

void uninstall() {
    if (!g_enabled.exchange(false)) return;
    puppet_hooks::uninstall();
    player_contact::uninstall();
}

bool inMoveScope(uintptr_t player) { return player != 0 && player == g_scopePlayer; }

bool active(uintptr_t player) {
    if (!g_enabled.load(std::memory_order_relaxed)) return false;
    const Character character = character_owner::identify(player);
    return character != Character::Unknown && character_owner::isRemoteOwned(character);
}

uint16_t ownerRoom() { return g_ownerRoom; }

void onFrame(const GameFrame& frame) {
    if (!g_enabled || static_cast<int>(frame.slot) != net_pad::peerSlot() || frame.slot >= kMaxPeerSlots) return;
    const uint32_t arrival = pose_wire::clockMs();
    std::lock_guard lock(g_mutex);
    if (static_cast<int>(frame.slot) != g_sourceSlot) {
        g_timeline.clear();
        g_clocks[frame.slot].clear();
        g_sourceSlot = frame.slot;
        g_ownerRoom = scene::kNone;
    }
    stream_clock::StreamClock& clock = g_clocks[frame.slot];
    if (frame.type == pose_wire::kMsgPoseState) {
        const auto pose = pose_wire::decodePose(frame.payload);
        if (!pose) return;
        g_timeline.addPose(clock, *pose, arrival);
        if (g_ownerRoom == scene::kNone || stream_clock::after(pose->seq, g_ownerRoomSeq)) {
            g_ownerRoomSeq = pose->seq;
            g_ownerRoom = pose->room;
            g_latestSight = {pose->room, pose->loadSerial, pose->group, pose->state, arrival};
        }
        g_hasPose = true;
        debug_stats::count(Counter::PosesReceived);
    } else if (const auto event = pose_wire::decodeEvent(frame.payload)) {
        g_timeline.addEvent(clock, *event, arrival);
        debug_stats::count(Counter::MotionEventsReceived);
    }
}

std::optional<peer_sight::Sight> latestPose(uint8_t slot) {
    std::lock_guard lock(g_mutex);
    if (static_cast<int>(slot) != g_sourceSlot || g_ownerRoom == scene::kNone) return std::nullopt;
    return g_latestSight;
}

std::optional<stream_clock::StreamClock> clockFor(uint8_t slot) {
    if (slot >= kMaxPeerSlots) return std::nullopt;
    std::lock_guard lock(g_mutex);
    if (!g_clocks[slot].ready()) return std::nullopt;
    return g_clocks[slot];
}

}  // namespace puppet
