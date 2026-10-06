#include "ds2/remote_update.h"

#include <atomic>
#include <cstdint>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/remote_player.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kPlayerComponentUpdate = 0x1407eea70;
constexpr uintptr_t kComponentOwner = 0x48;

// The calls of the update, by the bit that skips them. Every hooked function is shared with other code, so a hook skips
// only while the update of the remote's component is running on this thread.
enum Call : uint32_t {
    kEntityStep = 1u << 0,   // 0x1401201a0 (entity), when the entity is not flagged dead
    kStanceSync = 1u << 1,   // 0x1407eeea0 (component), three places
    kMotion = 1u << 2,       // 0x1407f2570 (component, dt), two places
    kStateTail = 1u << 3,    // 0x1407f1fc0 (component), two places, the last is a tail call
    kStateChange = 1u << 4,  // 0x1407ee900 (component, state id)
    kDeadTail = 1u << 5,     // 0x1407f11c0 (component, dt), the tail call of a dead or ragdolled player
    kQuery = 1u << 6,        // 0x140249b60 (entity), result used by the state change test
    kStateTest = 1u << 7,    // 0x1402fcfd0 (object, global, flag), result used by the state change test
    kBroadcast = 1u << 8,    // 0x140148d40 (global, id, value, flag), twice
    kMotionWhole = 1u << 9,  // not a skip: the motion call runs whole for the remote instead of its tail step only
};

thread_local bool t_remoteUpdate = false;
std::atomic<uint32_t> g_skip{0};

bool skipped(Call call) { return t_remoteUpdate && (g_skip.load() & call) != 0; }

using UpdateFn = uint64_t (*)(uintptr_t component, uintptr_t message);
using EntityFn = void (*)(uintptr_t);
using ComponentFn = void (*)(uintptr_t);
using DtFn = void (*)(uintptr_t, float);
using ChangeFn = void (*)(uintptr_t, uint32_t);
using QueryFn = uint64_t (*)(uintptr_t);
using TestFn = uint8_t (*)(uintptr_t, uintptr_t, uint8_t);
using BroadcastFn = void (*)(uintptr_t, uintptr_t, uintptr_t, uint8_t);
UpdateFn g_update = nullptr;
EntityFn g_entityStep = nullptr;
ComponentFn g_stanceSync = nullptr, g_stateTail = nullptr;
DtFn g_motion = nullptr, g_deadTail = nullptr;
ChangeFn g_stateChange = nullptr;
QueryFn g_query = nullptr;
TestFn g_stateTest = nullptr;
BroadcastFn g_broadcast = nullptr;

uint64_t updateDetour(uintptr_t component, uintptr_t message) {
    const uintptr_t remote = remote_player::entity();
    if (!remote || ds2::field<uintptr_t>(component, kComponentOwner) != remote) return g_update(component, message);
    if (g_skip.load() & remote_update::kSkipWholeUpdate) return 0;
    t_remoteUpdate = true;
    const uint64_t result = g_update(component, message);
    t_remoteUpdate = false;
    return result;
}

void entityStepDetour(uintptr_t entity) {
    if (!skipped(kEntityStep)) g_entityStep(entity);
}
void stanceSyncDetour(uintptr_t component) {
    if (!skipped(kStanceSync)) g_stanceSync(component);
}
// The motion call (0x1407f2570) has two parts: a block that looks up the player's current ability and re-selects it (what
// flickers Sam's use prompt when it runs for the remote) and, as its tail, the player state machine's step (vtable slot 8
// of the object at +0x1b8), which is what starts a ride. The remote runs the tail only.
constexpr uintptr_t kPlayerStateObject = 0x1b8;
constexpr uintptr_t kStateStepSlot = 0x40;

void motionTail(uintptr_t component, float dt) {
    const uintptr_t state = ds2::field<uintptr_t>(component, kPlayerStateObject);
    const uintptr_t vtable = decima::readPointer(state);
    const uintptr_t step = vtable ? ds2::field<uintptr_t>(vtable, kStateStepSlot) : 0;
    if (step) reinterpret_cast<void (*)(uintptr_t, float)>(step)(state, dt);
}

void motionDetour(uintptr_t component, float dt) {
    if (!t_remoteUpdate) {
        g_motion(component, dt);
        return;
    }
    const uint32_t skip = g_skip.load();
    if (skip & kMotion) return;
    if (skip & kMotionWhole) g_motion(component, dt);
    else motionTail(component, dt);
}
void stateTailDetour(uintptr_t component) {
    if (!skipped(kStateTail)) g_stateTail(component);
}
void stateChangeDetour(uintptr_t component, uint32_t id) {
    if (!skipped(kStateChange)) g_stateChange(component, id);
}
void deadTailDetour(uintptr_t component, float dt) {
    if (!skipped(kDeadTail)) g_deadTail(component, dt);
}
uint64_t queryDetour(uintptr_t entity) { return skipped(kQuery) ? 0 : g_query(entity); }
uint8_t stateTestDetour(uintptr_t object, uintptr_t global, uint8_t flag) {
    return skipped(kStateTest) ? 0 : g_stateTest(object, global, flag);
}
void broadcastDetour(uintptr_t global, uintptr_t id, uintptr_t value, uint8_t flag) {
    if (!skipped(kBroadcast)) g_broadcast(global, id, value, flag);
}

template <class Fn>
void hook(const char* name, uintptr_t address, void* detour, Fn* original) {
    hooks::install(name, ds2::at(address), detour, reinterpret_cast<void**>(original));
}

}  // namespace

namespace remote_update {

void setSkipMask(uint32_t mask) {
    g_skip = mask;
    logger::write("remote_update: the remote's player update skips mask %08x", mask);
}

void installEarly() {
    hook("player component update", kPlayerComponentUpdate, reinterpret_cast<void*>(&updateDetour), &g_update);
    hook("player update: entity step", 0x1401201a0, reinterpret_cast<void*>(&entityStepDetour), &g_entityStep);
    hook("player update: stance sync", 0x1407eeea0, reinterpret_cast<void*>(&stanceSyncDetour), &g_stanceSync);
    hook("player update: motion", 0x1407f2570, reinterpret_cast<void*>(&motionDetour), &g_motion);
    hook("player update: state tail", 0x1407f1fc0, reinterpret_cast<void*>(&stateTailDetour), &g_stateTail);
    hook("player update: state change", 0x1407ee900, reinterpret_cast<void*>(&stateChangeDetour), &g_stateChange);
    hook("player update: dead tail", 0x1407f11c0, reinterpret_cast<void*>(&deadTailDetour), &g_deadTail);
    hook("player update: query", 0x140249b60, reinterpret_cast<void*>(&queryDetour), &g_query);
    hook("player update: state test", 0x1402fcfd0, reinterpret_cast<void*>(&stateTestDetour), &g_stateTest);
    hook("player update: broadcast", 0x140148d40, reinterpret_cast<void*>(&broadcastDetour), &g_broadcast);
}

}  // namespace remote_update
