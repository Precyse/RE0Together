#include "puppet_hooks.h"

#include "game.h"
#include "hooks.h"
#include "pose_game.h"
#include "puppet.h"

namespace {

using MotionUpdateFunction = void(__fastcall*)(void* unit, void* edx);
using RumbleFunction = void(__fastcall*)(void* think, void* edx, uint32_t strength);

MotionUpdateFunction g_originalMotionUpdate = nullptr;
RumbleFunction g_originalRumble = nullptr;
bool g_installed = false;

void __fastcall motionUpdateDetour(void* unit, void* edx) {
    g_originalMotionUpdate(unit, edx);
    const uintptr_t player = reinterpret_cast<uintptr_t>(unit);
    if (puppet::inMoveScope(player)) pose_game::bendSpine(player);
}

// The character a think belongs to, when this is that character's current think.
uintptr_t ownerOf(uintptr_t think) {
    const uintptr_t owner = game::readPointer(think + game::kThinkOwnerOffset);
    return owner && game::readPointer(owner + game::kPlayerThinkOffset) == think ? owner : 0;
}

void __fastcall rumbleDetour(void* think, void* edx, uint32_t strength) {
    const uintptr_t owner = ownerOf(reinterpret_cast<uintptr_t>(think));
    if (owner && puppet::active(owner)) return;
    g_originalRumble(think, edx, strength);
}

}  // namespace

namespace puppet_hooks {

bool enable() {
    if (!hooks::install("unit motion update", game::kUnitMotionUpdateFunction, reinterpret_cast<void*>(&motionUpdateDetour),
                        reinterpret_cast<void**>(&g_originalMotionUpdate))) {
        return false;
    }
    if (!hooks::install("think rumble", game::kThinkRumbleFunction, reinterpret_cast<void*>(&rumbleDetour),
                        reinterpret_cast<void**>(&g_originalRumble))) {
        hooks::remove(game::kUnitMotionUpdateFunction);
        return false;
    }
    g_installed = true;
    return true;
}

void uninstall() {
    if (!g_installed) return;
    hooks::remove(game::kThinkRumbleFunction);
    hooks::remove(game::kUnitMotionUpdateFunction);
    g_installed = false;
}

}  // namespace puppet_hooks
