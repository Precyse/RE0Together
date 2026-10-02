#include "ds2/remote_camera.h"

#include <windows.h>

#include <cstring>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/remote_player.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kCameraComponentRecord = 0x144350600;  // DSThirdPersonPlayerCameraComponent
constexpr uintptr_t kModeRecord = 0x14435a610;             // DSCameraMode
constexpr size_t kModeSize = 0x2E40;
constexpr uintptr_t kAllocByRecord = 0x140103de0;  // (record) -> memory of the record's size
constexpr uintptr_t kModeCtor = 0x140f16ad0;
constexpr uintptr_t kModeInit = 0x140f19d20;
constexpr uintptr_t kModeInitIndexImm = 0x140f19d96;  // imm8 of the init's "player index == 0" comparison
constexpr uint8_t kLocalIndex = 0;
constexpr uint8_t kRemoteIndex = 0xFF;  // the remote keeps local index -1
constexpr uintptr_t kAddComponentFromResource = 0x140b468e0;  // (entity, component resource)

constexpr uintptr_t kComponentOwner = 0x48;        // component -> its entity
constexpr uintptr_t kComponentResource = 0x30;     // component -> its component resource
constexpr uintptr_t kResourceModeResource = 0x68;  // component resource -> the camera mode resource
constexpr uintptr_t kComponentLookup = 0x138;      // read by the mode init, set by the component's own Init
constexpr uintptr_t kComponentMode = 0x168;        // the mode the component built
constexpr uintptr_t kModeComponent = 0x2E28;
constexpr uintptr_t kModeResource = 0x2E30;
constexpr uintptr_t kModePlayerCamera = 0x58;  // the init's second index-0 lookup result: player + 0x290
constexpr uintptr_t kPlayerCameraBlock = 0x290;
constexpr uintptr_t kControllerMode = 0x7558;  // weak pointer to the controller's mode (mode + 0x20)
constexpr uintptr_t kWeakPointerOffset = 0x20;
constexpr uintptr_t kRefCount = 8;

constexpr uintptr_t kComponentInit = 0x140e39d90;
constexpr uintptr_t kComponentUpdate = 0x140e3c1a0;
constexpr uintptr_t kComponentTeleported = 0x140e3c0c0;
constexpr uintptr_t kModeBuilder = 0x140e3a740;

using AllocFn = uintptr_t (*)(uintptr_t record);
using CtorFn = uintptr_t (*)(uintptr_t self);
using InitModeFn = void (*)(uintptr_t mode);
using AddFromResourceFn = uintptr_t (*)(uintptr_t entity, uintptr_t resource);
using HandlerFn = void (*)(uintptr_t component, uintptr_t message);
using BuilderFn = void (*)(uintptr_t component, float dt);

thread_local bool t_spawning = false;
HandlerFn g_init = nullptr;
HandlerFn g_update = nullptr;
HandlerFn g_teleported = nullptr;
BuilderFn g_builder = nullptr;

bool belongsToRemote(uintptr_t component) {
    if (t_spawning) return true;
    const uintptr_t remote = remote_player::entity();
    return remote && ds2::field<uintptr_t>(component, kComponentOwner) == remote;
}

// The remote's component Init and Teleported build a mode bound to index 0 (Sam) and its Update runs Sam's AimIK.
void initDetour(uintptr_t component, uintptr_t message) {
    if (!belongsToRemote(component)) g_init(component, message);
}

void updateDetour(uintptr_t component, uintptr_t message) {
    if (!belongsToRemote(component)) g_update(component, message);
}

void teleportedDetour(uintptr_t component, uintptr_t message) {
    if (!belongsToRemote(component)) g_teleported(component, message);
}

// The mode builder is also called straight from DSPlayerEntity's update: on the remote, while its component has no
// mode yet, it would build one bound to Sam.
void builderDetour(uintptr_t component, float dt) {
    if (!ds2::field<uintptr_t>(component, kComponentMode) && belongsToRemote(component)) return;
    g_builder(component, dt);
}

// The remote entity has no camera component, so a mode built for it would share Sam's and corrupt it. Adds one from
// the resource of Sam's, and shares Sam's lookup table that the component's skipped Init would have set.
uintptr_t ensureCameraComponent(uintptr_t samMode) {
    const uintptr_t remote = remote_player::entity();
    if (const uintptr_t existing = ds2::componentByRecord(remote, kCameraComponentRecord)) return existing;
    const uintptr_t samComponent = ds2::field<uintptr_t>(samMode, kModeComponent);
    const uintptr_t resource = decima::readPointer(samComponent + kComponentResource);
    if (!resource) return 0;
    reinterpret_cast<AddFromResourceFn>(ds2::at(kAddComponentFromResource))(remote, resource);
    const uintptr_t component = ds2::componentByRecord(remote, kCameraComponentRecord);
    if (component) {
        ds2::field<uintptr_t>(component, kComponentLookup) = decima::readPointer(samComponent + kComponentLookup);
    }
    return component;
}

// Worker threads look up "the player with index 0" every frame, so the indices themselves must not change. The mode
// init's own lookup is patched to look for index -1, which only the remote has, for this one call.
bool initModeForRemote(uintptr_t mode) {
    auto* index = reinterpret_cast<uint8_t*>(ds2::at(kModeInitIndexImm));
    if (*index != kLocalIndex) {
        logger::write("remote_camera: the mode init's index lookup differs, not patching");
        return false;
    }
    DWORD protection = 0;
    VirtualProtect(index, 1, PAGE_EXECUTE_READWRITE, &protection);
    *index = kRemoteIndex;
    FlushInstructionCache(GetCurrentProcess(), index, 1);
    reinterpret_cast<InitModeFn>(ds2::at(kModeInit))(mode);
    *index = kLocalIndex;
    FlushInstructionCache(GetCurrentProcess(), index, 1);
    VirtualProtect(index, 1, protection, &protection);
    ds2::field<uintptr_t>(mode, kModePlayerCamera) = remote_player::player() + kPlayerCameraBlock;
    return true;
}

}  // namespace

namespace remote_camera {

SpawnScope::SpawnScope() { t_spawning = true; }

SpawnScope::~SpawnScope() { t_spawning = false; }

void installEarly() {
    hooks::install("camera component init", ds2::at(kComponentInit), reinterpret_cast<void*>(&initDetour),
                   reinterpret_cast<void**>(&g_init));
    hooks::install("camera component update", ds2::at(kComponentUpdate), reinterpret_cast<void*>(&updateDetour),
                   reinterpret_cast<void**>(&g_update));
    hooks::install("camera component teleported", ds2::at(kComponentTeleported),
                   reinterpret_cast<void*>(&teleportedDetour), reinterpret_cast<void**>(&g_teleported));
    hooks::install("camera mode builder", ds2::at(kModeBuilder), reinterpret_cast<void*>(&builderDetour),
                   reinterpret_cast<void**>(&g_builder));
}

bool give() {
    const uintptr_t samController = ds2::field<uintptr_t>(remote_player::samEntity(), ds2::kEntityController);
    const uintptr_t samMode = decima::readPointer(samController + kControllerMode) - kWeakPointerOffset;
    const uintptr_t component = ensureCameraComponent(samMode);
    const uintptr_t componentResource = component ? decima::readPointer(component + kComponentResource) : 0;
    const uintptr_t resource = decima::readPointer(componentResource + kResourceModeResource);
    if (!component || !resource) {
        logger::write("remote_camera: Sam's camera is not readable (mode %p)", reinterpret_cast<void*>(samMode));
        return false;
    }
    const uintptr_t mode = reinterpret_cast<AllocFn>(ds2::at(kAllocByRecord))(ds2::at(kModeRecord));
    std::memset(reinterpret_cast<void*>(mode), 0, kModeSize);
    reinterpret_cast<CtorFn>(ds2::at(kModeCtor))(mode);
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(resource + kRefCount));
    ds2::field<uintptr_t>(mode, kModeResource) = resource;
    ds2::field<uintptr_t>(mode, kModeComponent) = component;
    ds2::field<uintptr_t>(component, kComponentMode) = mode;
    return initModeForRemote(mode);
}

}  // namespace remote_camera
