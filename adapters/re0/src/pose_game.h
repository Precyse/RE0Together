#pragma once
#include <cstddef>
#include <cstdint>

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

// Called on the game thread after the engine ran a motion request on the character whose anim control is `animControl`.
using SetObserver = void (*)(uintptr_t animControl, const MotionRequest& request);

// Hooks cAnimControl::set on the first call (one MinHook for all) and adds `observer`, which sees every request the
// engine makes (not those of `replay`). At most kMaxAnimObservers; call after the game code is decrypted. False when the
// hook could not be installed or the list is full.
constexpr size_t kMaxAnimObservers = 4;
bool addAnimObserver(SetObserver observer);
void uninstallAnimHook();

// The anim control of a character object.
inline uintptr_t animControlOf(uintptr_t player) { return player + game::kAnimControlOffset; }

// The character's current pose (position, rotation, both layers, think, weapon, status, +0x1034, hit attributes, aim
// pitch and tier); the caller adds time, number, room and load serial.
bool readPose(uintptr_t player, pose_wire::Pose& out);

// The character's think group, state and phase into `out`; false unless its think is a cPlayerThink.
bool readThink(uintptr_t player, pose_wire::Pose& out);

// The character's layer 0 as the puppet rule compares it.
bool readLayer0(uintptr_t player, puppet_rule::Layer0& out);

// True when the character's motion banks hold the motion (the engine's own check).
bool motionAvailable(uintptr_t player, uint16_t motionNo);

// Runs a motion request on the character through the engine's own function, not seen by the observer.
bool replay(uintptr_t player, const MotionRequest& request);

// Writes position and rotation, makes the motion's own displacement cancel, and mirrors the owner's think state (when
// the puppet's think is a cPlayerThink).
void writePose(uintptr_t player, const pose_wire::Pose& pose);

// Sends the character back to neutral the way the script's Player_Reset does.
bool resetPlayer(uintptr_t player);

// sGameInfo mode flags, 0 when unreadable.
uint32_t modeFlags();

// Sets the global script-pause bit; releasePause clears it unless it was already set by the game.
void holdPause();
void releasePause();

}  // namespace pose_game
