#include "pose_game.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

#include "crash_dump.h"
#include "game.h"
#include "hooks.h"
#include "log.h"

namespace {

using SetFunction = void(__fastcall*)(void* animControl, void* edx, uint32_t layer, uint32_t motionNo, uint32_t flags,
                                      float interp, float start);

constexpr uint32_t kMaxByte = 0xff;

SetFunction g_originalSet = nullptr;
bool g_hookInstalled = false;
std::array<pose_game::SetObserver, pose_game::kMaxAnimObservers> g_observers{};
std::atomic<size_t> g_observerCount{0};  // observers published to the game thread
uintptr_t g_pauseAddress = 0;  // where the bit was set; 0 while not held
bool g_pauseWasSet = false;

void __fastcall setDetour(void* animControl, void* edx, uint32_t layer, uint32_t motionNo, uint32_t flags, float interp,
                          float start) {
    g_originalSet(animControl, edx, layer, motionNo, flags, interp, start);
    const size_t count = g_observerCount.load(std::memory_order_acquire);
    for (size_t i = 0; i < count; ++i) g_observers[i](reinterpret_cast<uintptr_t>(animControl), {layer, motionNo, flags, interp, start});
}

uintptr_t layerAddress(uintptr_t player, size_t layer) {
    return player + game::kMotionLayerBaseOffset + layer * game::kMotionLayerStride;
}

bool readLayer(uintptr_t player, size_t layer, pose_wire::Layer& out) {
    const uintptr_t base = layerAddress(player, layer);
    return game::readMemory(base + game::kMotionNumberOffset, out.motion) &&
           game::readMemory(base + game::kMotionFrameOffset, out.frame);
}

// The player think when it is a cPlayerThink (the layout of group / state / phase below), else 0: a partner AI
// (cPlayerSubThink) keeps other data at these offsets.
uintptr_t thinkOf(uintptr_t player) {
    const uintptr_t think = game::readPointer(player + game::kPlayerThinkOffset);
    return think && game::readPointer(think) == game::kControlledThinkVtable ? think : 0;
}

uint8_t asByte(int32_t value) { return static_cast<uint8_t>(std::clamp<int32_t>(value, 0, kMaxByte)); }

uint8_t clampedByte(int32_t value, uint8_t most) { return static_cast<uint8_t>(std::clamp<int32_t>(value, 0, most)); }

// The fields POSE_STATE v2 adds to the base pose, each left at 0 when unreadable like the status flags.
void readExtras(uintptr_t player, pose_wire::Pose& out) {
    out.x1034 = 0;
    game::readMemory(player + game::kPlayerX1034Offset, out.x1034);
    uint32_t hit = 0;
    game::readMemory(player + game::kPlayerHitAttributesOffset, hit);
    out.hitAttr = static_cast<uint16_t>(hit & pose_wire::kHitAttributeMask);
    float pitch = 0.0f;
    game::readMemory(player + game::kPlayerManualPitchOffset, pitch);
    out.manualPitch = std::isfinite(pitch) ? pitch : 0.0f;
    int32_t tier = 0;
    game::readMemory(player + game::kPlayerAimTierOffset, tier);
    out.aimTier = clampedByte(tier, pose_wire::kMaxAimTier);
}

bool readThinkByte(uintptr_t think, uintptr_t offset, uint8_t& out) {
    int32_t value = 0;
    if (!game::readMemory(think + offset, value)) return false;
    out = asByte(value);
    return true;
}

bool callSet(void* animControl, const pose_game::MotionRequest& request) {
    __try {
        g_originalSet(animControl, nullptr, request.layer, request.motionNo, request.flags, request.interp, request.start);
        return true;
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "pose_game: cAnimControl::set")) {
        return false;
    }
}

bool callMotionAvailable(uintptr_t player, uint16_t motionNo) {
    __try {
        return game::callThiscall<uintptr_t>(game::kMotionAvailableFunction, reinterpret_cast<void*>(player),
                                             static_cast<uint32_t>(motionNo)) != 0;
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "pose_game: motion available")) {
        return false;
    }
}

bool callReset(uintptr_t player) {
    __try {
        game::callThiscall<void>(game::kPlayerResetFunction, reinterpret_cast<void*>(player));
        return true;
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "pose_game: Player_Reset")) {
        return false;
    }
}

uintptr_t modeFlagsAddress() {
    const uintptr_t info = game::readPointer(game::kGameInfoGlobal);
    return info ? info + game::kGameInfoModeFlagsOffset : 0;
}

}  // namespace

namespace pose_game {

bool addAnimObserver(SetObserver observer) {
    if (!g_hookInstalled &&
        !hooks::install("cAnimControl::set", game::kAnimControlSetFunction, reinterpret_cast<void*>(&setDetour),
                        reinterpret_cast<void**>(&g_originalSet))) {
        return false;
    }
    g_hookInstalled = true;
    const size_t count = g_observerCount.load();
    if (count >= kMaxAnimObservers) return false;
    g_observers[count] = observer;
    g_observerCount.store(count + 1, std::memory_order_release);
    return true;
}

void uninstallAnimHook() {
    if (!g_hookInstalled) return;
    hooks::remove(game::kAnimControlSetFunction);
    g_hookInstalled = false;
    g_observerCount = 0;
}

bool readPose(uintptr_t player, pose_wire::Pose& out) {
    if (!game::readTransform(player, out.pos, out.quat) || !readLayer(player, 0, out.layer[0]) ||
        !readLayer(player, 1, out.layer[1])) {
        return false;
    }
    int32_t weapon = 0;
    game::readMemory(player + game::kPlayerWeaponTypeOffset, weapon);
    out.weapon = clampedByte(weapon, pose_wire::kMaxWeaponType);
    out.status = 0;
    game::readMemory(player + game::kPlayerStatusFlagsOffset, out.status);
    readExtras(player, out);
    if (!readThink(player, out)) out.group = out.state = out.phase = 0;
    out.group = std::min(out.group, pose_wire::kMaxThinkGroup);
    return true;
}

bool readThink(uintptr_t player, pose_wire::Pose& out) {
    const uintptr_t think = thinkOf(player);
    return think && readThinkByte(think, game::kThinkGroupOffset, out.group) &&
           readThinkByte(think, game::kThinkStateOffset, out.state) && readThinkByte(think, game::kThinkPhaseOffset, out.phase);
}

bool readLayer0(uintptr_t player, puppet_rule::Layer0& out) {
    const uintptr_t base = layerAddress(player, 0);
    return game::readMemory(base + game::kMotionNumberOffset, out.motion) &&
           game::readMemory(base + game::kMotionFrameOffset, out.frame) &&
           game::readMemory(base + game::kMotionFrameMaxOffset, out.frameMax);
}

bool motionAvailable(uintptr_t player, uint16_t motionNo) { return callMotionAvailable(player, motionNo); }

bool replay(uintptr_t player, const MotionRequest& request) {
    return g_originalSet && callSet(reinterpret_cast<void*>(animControlOf(player)), request);
}

void writePose(uintptr_t player, const pose_wire::Pose& pose) {
    game::writeTransform(player, pose.pos, pose.quat);
    game::writeMemory(player + game::kPlayerKeepRootMotionOffset, uint8_t{0});
    const uintptr_t think = thinkOf(player);
    if (!think) return;
    game::writeMemory(think + game::kThinkGroupOffset, static_cast<int32_t>(pose.group));
    game::writeMemory(think + game::kThinkStateOffset, static_cast<int32_t>(pose.state));
    game::writeMemory(think + game::kThinkPhaseOffset, static_cast<int32_t>(pose.phase));
}

bool resetPlayer(uintptr_t player) { return callReset(player); }

uint32_t modeFlags() {
    uint32_t flags = 0;
    game::readMemory(modeFlagsAddress(), flags);
    return flags;
}

void holdPause() {
    const uintptr_t address = modeFlagsAddress();
    uint32_t flags = 0;
    if (!game::readMemory(address, flags)) return;
    g_pauseWasSet = (flags & game::kPlayerPauseBit) != 0;
    if (game::writeMemory(address, flags | game::kPlayerPauseBit)) g_pauseAddress = address;
}

void releasePause() {
    uint32_t flags = 0;
    if (g_pauseAddress && !g_pauseWasSet && game::readMemory(g_pauseAddress, flags)) {
        game::writeMemory(g_pauseAddress, flags & ~game::kPlayerPauseBit);
    }
    g_pauseAddress = 0;
}

}  // namespace pose_game
