// DEATH STRANDING 2: the partner's body, a second player entity. It is created the way the engine creates a network
// player (a NetPlayerGame, added to the player manager, its character spawned from Sam's entity resource), then given
// its own camera, a costume and silenced components (remote_camera, remote_appearance, remote_guards). The engine's own
// state machine drives it; each frame it is placed where the partner's smoothed pose says, or it rides a vehicle
// (remote_ride). See docs/DS2_NOTES.md, "A second player entity".
#include <windows.h>

#include <atomic>
#include <cmath>
#include <chrono>
#include <cstring>
#include <mutex>

#include "decima/safe_read.h"
#include "decima/world_transform.h"
#include "ds2/engine.h"
#include "ds2/player_state.h"
#include "ds2/remote_animation.h"
#include "ds2/remote_appearance.h"
#include "ds2/remote_camera.h"
#include "ds2/remote_context.h"
#include "ds2/remote_guards.h"
#include "ds2/remote_player.h"
#include "ds2/remote_ride.h"
#include "ds2/setdriver_guard.h"
#include "ds2/sim_tick.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "remote_body.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr uintptr_t kPlayerManagerGlobal = 0x14623DF40;  // [global] = the PlayerManager
constexpr uintptr_t kManagerLocalPlayer = 0x48;
constexpr uintptr_t kPlayerEntity = 0x48;

// A NetPlayerGame is allocated by its type record, zeroed, constructed, and wraps the PlayerGame at +0x58.
constexpr uintptr_t kAllocByRecord = 0x140103de0;
constexpr uintptr_t kNetPlayerRecord = 0x144281ae0;
constexpr uintptr_t kNetPlayerConstruct = 0x14074ac30;
constexpr size_t kNetPlayerSize = 0x60;
constexpr uintptr_t kNetPlayerGame = 0x58;
constexpr uintptr_t kNetPlayerLink = 0x8;      // copied from Sam's: [[player + 0xE8] + 0x8]
constexpr uintptr_t kPlayerLinkSource = 0xE8;
constexpr uintptr_t kNetPlayerRemoteFlag = 0x32;
constexpr uintptr_t kNetPlayerSpawnedFlag = 0x40;
// What the remote's PlayerGame shares with Sam's (PlayerResourceGame and the AIFaction references), each reference
// counted, and its entity resource, which the engine's setter references.
constexpr uintptr_t kSharedReferences[] = {0x30, 0x60, 0x68, 0xB8};
constexpr uintptr_t kRefCount = 8;
constexpr uintptr_t kPlayerEntityResource = 0x148;
constexpr uintptr_t kSetEntityResource = 0x140752c70;  // (player, resource reference)
constexpr uintptr_t kRequestSpawn = 0x140751bc0;       // (player, spawn at the given transform, transform)
constexpr uintptr_t kManagerAddPlayer = 8;             // vtable offset of PlayerManager::AddPlayer


constexpr double kSpawnAhead = 2.5;  // metres in front of Sam and
constexpr double kSpawnRight = 1.2;  // to his right; the first placement moves the body to the partner
constexpr int kControllerWaitFrames = 300;
constexpr auto kTargetStale = std::chrono::milliseconds(500);
constexpr uint8_t kNoSlot = 0xFF;

enum class Stage { Idle, WaitController, Live, Failed };

using AllocFn = uintptr_t (*)(uintptr_t record);
using CtorFn = uintptr_t (*)(uintptr_t self);
using AddPlayerFn = void (*)(uintptr_t manager, uintptr_t player);
using SetResourceFn = void (*)(uintptr_t player, uintptr_t reference);
using RequestSpawnFn = void (*)(uintptr_t player, bool atTransform, const decima::WorldTransform* transform);

std::atomic<bool> g_enabled{false};
Stage g_stage = Stage::Idle;
uintptr_t g_netPlayer = 0;
std::atomic<uintptr_t> g_entity{0};
int g_waitedFrames = 0;
ds2::GameplayClock g_gameplay;
uintptr_t g_samAtSpawn = 0;

// The partner's pose from the render thread, applied on the update thread.
std::mutex g_targetMutex;
uint8_t g_slot = kNoSlot;
game::Pose g_target;
world_to_screen::Vec3 g_targetVelocity;
Clock::time_point g_targetAt;

uintptr_t playerManager() { return decima::readPointer(ds2::at(kPlayerManagerGlobal)); }

uintptr_t samPlayer() { return decima::readPointer(playerManager() + kManagerLocalPlayer); }

uintptr_t samEntity() { return decima::readPointer(samPlayer() + kPlayerEntity); }

uintptr_t remotePlayer() { return g_netPlayer ? decima::readPointer(g_netPlayer + kNetPlayerGame) : 0; }

bool targetFresh() {
    std::lock_guard lock(g_targetMutex);
    return g_slot != kNoSlot && Clock::now() - g_targetAt <= kTargetStale;
}

// Creates the remote's PlayerGame and spawns its character beside Sam. The camera hooks treat this thread's component
// calls as the remote's until the entity exists.
void spawn() {
    const uintptr_t manager = playerManager();
    const uintptr_t sam = samPlayer();
    const uintptr_t memory = reinterpret_cast<AllocFn>(ds2::at(kAllocByRecord))(ds2::at(kNetPlayerRecord));
    std::memset(reinterpret_cast<void*>(memory), 0, kNetPlayerSize);
    g_netPlayer = reinterpret_cast<CtorFn>(ds2::at(kNetPlayerConstruct))(memory);
    const uintptr_t player = decima::readPointer(g_netPlayer + kNetPlayerGame);
    ds2::field<uintptr_t>(g_netPlayer, kNetPlayerLink) =
        decima::readPointer(decima::readPointer(sam + kPlayerLinkSource) + kNetPlayerLink);
    ds2::field<uint8_t>(g_netPlayer, kNetPlayerRemoteFlag) = 1;
    for (const uintptr_t offset : kSharedReferences) {
        const uintptr_t object = decima::readPointer(sam + offset);
        if (object) InterlockedIncrement(reinterpret_cast<volatile LONG*>(object + kRefCount));
        ds2::field<uintptr_t>(player, offset) = object;
    }
    reinterpret_cast<SetResourceFn>(ds2::at(kSetEntityResource))(player, sam + kPlayerEntityResource);
    const auto addPlayer = reinterpret_cast<AddPlayerFn>(decima::readPointer(decima::readPointer(manager) + kManagerAddPlayer));
    addPlayer(manager, player);

    decima::WorldTransform at{};
    decima::safeRead(samEntity() + ds2::kEntityTransform, at);
    const float* right = at.orientation.row[0];
    const float* forward = at.orientation.row[1];
    at.position.x += forward[0] * kSpawnAhead + right[0] * kSpawnRight;
    at.position.y += forward[1] * kSpawnAhead + right[1] * kSpawnRight;
    remote_appearance::onSpawned();
    {
        remote_camera::SpawnScope scope;
        reinterpret_cast<RequestSpawnFn>(ds2::at(kRequestSpawn))(player, true, &at);
    }
    g_entity = decima::readPointer(player + kPlayerEntity);
    ds2::field<uint8_t>(g_netPlayer, kNetPlayerSpawnedFlag) = 0;
    remote_guards::silenceRemote();
    g_waitedFrames = 0;
    g_stage = Stage::WaitController;
    logger::write("remote_body: spawned player %p entity %p", reinterpret_cast<void*>(player),
                  reinterpret_cast<void*>(g_entity.load()));
}

void finishSpawn() {
    if (ds2::field<uintptr_t>(g_entity.load(), ds2::kEntityController)) {
        g_stage = remote_camera::give() ? Stage::Live : Stage::Failed;
        logger::write("remote_body: %s", g_stage == Stage::Live ? "live" : "no camera, off");
    } else if (++g_waitedFrames > kControllerWaitFrames) {
        logger::write("remote_body: no controller after %d frames, off", g_waitedFrames);
        g_stage = Stage::Failed;
    }
}

// Loopback test of the animation mirror: the body stands beside the local player, so both poses can be compared.
constexpr double kLoopbackBeside = 1.3;  // metres to the local player's right

bool loopbackPose(game::Pose& pose) {
    decima::WorldTransform sam{};
    if (!decima::safeRead(samEntity() + ds2::kEntityTransform, sam)) return false;
    const float* right = sam.orientation.row[0];
    const float* forward = sam.orientation.row[1];
    pose.position = {sam.position.x + right[0] * kLoopbackBeside, sam.position.y + right[1] * kLoopbackBeside,
                     sam.position.z};
    pose.yaw = std::atan2(forward[0], forward[1]);
    return true;
}

void follow() {
    game::Pose pose;
    world_to_screen::Vec3 velocity;
    if (remote_animation::mirrorsLocalPlayer() && loopbackPose(pose)) {
        game::placeBody(g_entity.load(), pose, {});
        return;
    }
    {
        std::lock_guard lock(g_targetMutex);
        if (Clock::now() - g_targetAt > kTargetStale) return;
        pose = g_target;
        velocity = g_targetVelocity;
    }
    game::placeBody(g_entity.load(), pose, velocity);
}

// The world the remote lived in is gone (return to title, another load): the engine destroys its entities with it,
// so only this module's references are dropped, and the body is created again once gameplay resumes.
void forgetBody() {
    logger::write("remote_body: gameplay ended, body forgotten");
    g_entity = 0;
    g_netPlayer = 0;
    g_samAtSpawn = 0;
    remote_ride::reset();
    g_stage = Stage::Idle;
}

void advance() {
    g_gameplay.update(samEntity());
    if (g_stage != Stage::Idle && (!g_gameplay.active() || samEntity() != g_samAtSpawn)) forgetBody();
    switch (g_stage) {
        case Stage::Idle:
            if (g_enabled.load() && g_gameplay.settled() && targetFresh()) {
                g_samAtSpawn = samEntity();
                spawn();
                finishSpawn();  // the camera must exist before the engine updates the remote for the first time
            }
            break;
        case Stage::WaitController:
            finishSpawn();
            break;
        case Stage::Live:
            remote_ride::tick();
            if (!remote_ride::holdsBody()) follow();
            break;
        case Stage::Failed:
            break;
    }
}

}  // namespace

namespace remote_player {

uintptr_t player() { return remotePlayer(); }
uintptr_t entity() { return g_entity.load(); }
uintptr_t samEntity() { return ::samEntity(); }

uint8_t slot() {
    std::lock_guard lock(g_targetMutex);
    return g_slot;
}

bool isLive() { return g_stage == Stage::Live; }

}  // namespace remote_player

namespace remote_body {

void setEnabled(bool enabled) { g_enabled = enabled; }

void installEarly() {
    remote_context::install();
    setdriver_guard::install();
    remote_guards::installEarly();
    remote_camera::installEarly();
    remote_appearance::installEarly();
    remote_animation::installEarly();
    remote_ride::installEarly();
    sim_tick::add(&advance);
}

std::optional<uint64_t> ownerKey() {
    const uintptr_t entity = g_entity.load();
    if (g_stage != Stage::Live || !entity) return std::nullopt;
    return ds2::field<uint64_t>(entity, ds2::kEntityNetworkId);
}

uint8_t slot() { return remote_player::slot(); }

void setTarget(uint8_t slot, const game::Pose& pose, const world_to_screen::Vec3& velocity) {
    if (!g_enabled.load()) return;
    std::lock_guard lock(g_targetMutex);
    if (g_slot != slot && g_slot != kNoSlot && Clock::now() - g_targetAt <= kTargetStale) return;  // another partner
    g_slot = slot;
    g_target = pose;
    g_targetVelocity = velocity;
    g_targetAt = Clock::now();
}

}  // namespace remote_body
