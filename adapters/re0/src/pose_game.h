#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>

#include "game.h"
#include "pose_wire.h"
#include "puppet_rule.h"

// The engine side of the pose puppet (partner_mode=state): reads a character's pose, sees and replays its motion
// requests, holds the script pause for one move. Every game access is exception-safe; a fault is reported through
// crash_dump::reportCaught and the call answers false.
namespace pose_game {

// The arguments of one cAnimControl::set call.
struct MotionRequest {
    uint32_t layer;
    uint32_t motionNo;
    uint32_t flags;
    float interp;
    float start;
};

// Called on the game thread (a request made on any other thread is not reported) after the engine ran a motion request on the character whose anim control is `animControl`.
using SetObserver = void (*)(uintptr_t animControl, const MotionRequest& request);

// Hooks cAnimControl::set on the first call (one MinHook for all) and adds `observer`, which sees every request the
// engine makes (not those of `replay`). At most kMaxAnimObservers; call after the game code is decrypted. False when the
// hook could not be installed or the list is full.
constexpr size_t kMaxAnimObservers = 4;
bool addAnimObserver(SetObserver observer);
void uninstallAnimHook();

// The MotionEvent target (pose_wire: 0 body, 1 + k sub-control k) of `animControl` when it is one of `player`'s own.
std::optional<uint16_t> controlTarget(uintptr_t player, uintptr_t animControl);

// True when the anim control `target` names has its unit: always for the body, for a sub-control once a weapon is attached.
bool hasUnit(uintptr_t player, uint16_t target);

// The character's current pose (position, rotation, both layers, think, weapon, status, +0x1034, hit attributes, aim
// pitch and tier); the caller adds time, number, room and load serial.
bool readPose(uintptr_t player, pose_wire::Pose& out);

// The character's think group, state and phase into `out`; false unless its think is a cPlayerThink.
bool readThink(uintptr_t player, pose_wire::Pose& out);

// Motion layer `layer` of the character (0 body, 1 overlay) as the puppet rule compares it.
bool readLayerFrame(uintptr_t player, size_t layer, puppet_rule::LayerFrame& out);

// True when the motion banks of the unit the anim control `target` names hold the motion (the engine's own check); false
// for a sub-control without a unit.
bool motionAvailable(uintptr_t player, uint16_t target, uint16_t motionNo);

// Runs a motion request on the character's anim control `target` through the engine's own function, not seen by the
// observer.
bool replay(uintptr_t player, uint16_t target, const MotionRequest& request);

// Writes position and rotation, makes the motion's own displacement cancel, mirrors the owner's status flags (under the
// character's lock), hit attributes (by diff through the engine's setter and clearer), +0x1034, manual pitch and aim
// tier, and the owner's think state (when the puppet's think is a cPlayerThink).
void writePose(uintptr_t player, const pose_wire::Pose& pose);

// Engine steps the paused branch of a character's move skips, each exception-safe (false after a fault).
// The hand pose from the current motion and weapon: playMotion does it, a replayed request does not.
bool refreshHandPose(uintptr_t player);
// The light probe, before the move.
bool probeLight(uintptr_t player);
// The spine bend by the manual pitch, right after the move's motion update.
bool bendSpine(uintptr_t player);
// The frame-driven hand / holster swaps and the area id, after the move.
bool settleAfterMove(uintptr_t player);

// Sends the character back to neutral the way the script's Player_Reset does.
bool resetPlayer(uintptr_t player);

// sGameInfo mode flags, 0 when unreadable.
uint32_t modeFlags();

// Sets the global script-pause bit; releasePause clears it unless it was already set by the game.
void holdPause();
void releasePause();

}  // namespace pose_game
