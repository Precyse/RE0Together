// DEATH STRANDING 2 (Steam build 23923251) implementation of game.h. Code locations are found by byte pattern at
// start-up; field offsets come from the engine's RTTI and from the code the patterns match (docs/DS2_NOTES.md).
#include "game.h"

#include <cmath>
#include <cstring>

#include "decima/safe_read.h"
#include "decima/world_transform.h"
#include "log.h"
#include "pattern_scan.h"

namespace {

// Player::GetLocalPlayer(int): `mov rcx, [PlayerManager]` at +10, `lea rdx, [LocalPlayerFilter]` at +33.
constexpr const char* kGetLocalPlayer =
    "89 4C 24 08 48 83 EC 58 8B D1 48 8B 0D ?? ?? ?? ?? 48 85 C9 75 07 33 C0 48 83 C4 58 C3 85 D2 75 ?? 48 8D 15";
constexpr int kManagerDisp = 13, kManagerEnd = 17;
// Player::GetLastActivatedCamera: the camera stack's count and data offsets inside Player.
constexpr const char* kLastActivatedCamera =
    "48 63 87 ?? ?? ?? ?? 85 C0 75 04 33 FF EB ?? 48 8B D0 48 8B 87 ?? ?? ?? ?? 48 C1 E2 07";
constexpr int kCameraCountDisp = 3, kCameraDataDisp = 21;

constexpr uintptr_t kManagerLocalPlayers = 0x48;  // PlayerManager: Player* per local player (index 0 = this machine)
constexpr uintptr_t kPlayerEntity = 0x48;         // Player::GetEntity
constexpr uintptr_t kCameraStackStride = 0x80;    // `shl rdx, 7` in GetLastActivatedCamera
constexpr uintptr_t kCameraWeakTarget = 0x20;     // the stack holds the camera's WeakPtrRTTITarget base
constexpr uintptr_t kEntityTransform = 0xE8;      // Entity.Orientation (WorldTransform), RTTI
constexpr uintptr_t kCameraFov = 0x3D4;           // CameraEntity.FOV (degrees, horizontal: checked against the image), RTTI
constexpr uintptr_t kCameraNear = 0x43C;          // CameraEntity.NearPlane, RTTI
constexpr int kMaxCameraStack = 64;
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
constexpr double kMinFovDegrees = 1.0, kMaxFovDegrees = 179.0;

// RotMatrix rows: right, forward, up (Decima is Z-up, Y-forward).
constexpr int kRightRow = 0, kForwardRow = 1, kUpRow = 2;

uintptr_t g_managerGlobal = 0;
uint32_t g_cameraCountOffset = 0;
uint32_t g_cameraDataOffset = 0;

uintptr_t localPlayerObject() {
    const uintptr_t manager = decima::readPointer(g_managerGlobal);
    return manager ? decima::readPointer(manager + kManagerLocalPlayers) : 0;
}

bool readTransform(uintptr_t entity, decima::WorldTransform& out) {
    return entity && decima::safeRead(entity + kEntityTransform, out) && std::isfinite(out.position.x);
}

world_to_screen::Vec3 row(const decima::RotMatrix& m, int r) { return {m.row[r][0], m.row[r][1], m.row[r][2]}; }

uintptr_t lastActivatedCamera(uintptr_t player) {
    int32_t count = 0;
    if (!decima::safeRead(player + g_cameraCountOffset, count) || count <= 0 || count > kMaxCameraStack) return 0;
    const uintptr_t data = decima::readPointer(player + g_cameraDataOffset);
    const uintptr_t target = data ? decima::readPointer(data + (count - 1) * kCameraStackStride) : 0;
    return target ? target - kCameraWeakTarget : 0;
}

bool findCode() {
    const uintptr_t getLocalPlayer = pattern_scan::find(kGetLocalPlayer);
    const uintptr_t cameraCode = pattern_scan::find(kLastActivatedCamera);
    if (!getLocalPlayer || !cameraCode) {
        logger::write("game: code patterns not found (GetLocalPlayer %d, camera %d): unsupported build",
                      getLocalPlayer != 0, cameraCode != 0);
        return false;
    }
    g_managerGlobal = pattern_scan::ripTarget(getLocalPlayer, kManagerDisp, kManagerEnd);
    std::memcpy(&g_cameraCountOffset, reinterpret_cast<const void*>(cameraCode + kCameraCountDisp), sizeof(uint32_t));
    std::memcpy(&g_cameraDataOffset, reinterpret_cast<const void*>(cameraCode + kCameraDataDisp), sizeof(uint32_t));
    logger::write("game: player manager %p, camera stack +0x%x/+0x%x", reinterpret_cast<void*>(g_managerGlobal),
                  g_cameraCountOffset, g_cameraDataOffset);
    return true;
}

}  // namespace

namespace game {

bool resolve() {
    static const bool codeFound = findCode();
    return codeFound && localPlayerObject() != 0;  // false until the player is in the world
}

std::optional<Pose> localPlayer() {
    const uintptr_t player = localPlayerObject();
    decima::WorldTransform t;
    if (!player || !readTransform(decima::readPointer(player + kPlayerEntity), t)) return std::nullopt;
    const world_to_screen::Vec3 forward = row(t.orientation, kForwardRow);
    return Pose{{t.position.x, t.position.y, t.position.z}, static_cast<float>(std::atan2(forward.x, forward.y))};
}

std::optional<world_to_screen::Camera> camera() {
    const uintptr_t player = localPlayerObject();
    const uintptr_t cam = player ? lastActivatedCamera(player) : 0;
    decima::WorldTransform t;
    float fovDegrees = 0, nearPlane = 0;
    if (!readTransform(cam, t) || !decima::safeRead(cam + kCameraFov, fovDegrees) ||
        !decima::safeRead(cam + kCameraNear, nearPlane) || fovDegrees < kMinFovDegrees || fovDegrees > kMaxFovDegrees) {
        return std::nullopt;
    }
    world_to_screen::Camera c;
    c.position = {t.position.x, t.position.y, t.position.z};
    c.right = row(t.orientation, kRightRow);
    c.forward = row(t.orientation, kForwardRow);
    c.up = row(t.orientation, kUpRow);
    c.horizontalFovRadians = fovDegrees * kDegreesToRadians;
    c.nearPlane = nearPlane;
    return c;
}

}  // namespace game
