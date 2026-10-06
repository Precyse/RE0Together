#include "ds2/remote_update.h"

#include <cstdint>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/remote_player.h"
#include "hooks.h"

namespace {

constexpr uintptr_t kPlayerComponentUpdate = 0x1407eea70;
constexpr uintptr_t kPlayerMotion = 0x1407f2570;  // (component, dt)
constexpr uintptr_t kComponentOwner = 0x48;
constexpr uintptr_t kPlayerStateObject = 0x1b8;  // the component's player state machine object
constexpr uintptr_t kStateStepSlot = 0x40;       // its per-frame step, the motion call's tail

using UpdateFn = uint64_t (*)(uintptr_t component, uintptr_t message);
using MotionFn = void (*)(uintptr_t component, float dt);
UpdateFn g_update = nullptr;
MotionFn g_motion = nullptr;

// Every hooked function is shared with Sam, so the motion detour acts only inside the update of the remote's component.
thread_local bool t_remoteUpdate = false;

uint64_t updateDetour(uintptr_t component, uintptr_t message) {
    const uintptr_t remote = remote_player::entity();
    if (!remote || ds2::field<uintptr_t>(component, kComponentOwner) != remote) return g_update(component, message);
    t_remoteUpdate = true;
    const uint64_t result = g_update(component, message);
    t_remoteUpdate = false;
    return result;
}

// The state object can be freed under an update that runs while the body is taken down; its first qword is then whatever
// reused the memory (a pointer into text once made the step a call to "bject"), so the vtable and the step are checked.
void stateStep(uintptr_t component, float dt) {
    const uintptr_t state = ds2::field<uintptr_t>(component, kPlayerStateObject);
    const uintptr_t vtable = decima::readPointer(state);
    uintptr_t step = 0;
    if (!ds2::inGameImage(vtable) || !decima::safeRead(vtable + kStateStepSlot, step) || !ds2::inGameImage(step)) return;
    reinterpret_cast<void (*)(uintptr_t, float)>(step)(state, dt);
}

void motionDetour(uintptr_t component, float dt) {
    if (t_remoteUpdate) stateStep(component, dt);
    else g_motion(component, dt);
}

}  // namespace

namespace remote_update {

void installEarly() {
    hooks::install("player component update", ds2::at(kPlayerComponentUpdate), reinterpret_cast<void*>(&updateDetour),
                   reinterpret_cast<void**>(&g_update));
    hooks::install("player motion", ds2::at(kPlayerMotion), reinterpret_cast<void*>(&motionDetour),
                   reinterpret_cast<void**>(&g_motion));
}

}  // namespace remote_update
