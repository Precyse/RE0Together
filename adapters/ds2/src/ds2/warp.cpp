// DEATH STRANDING 2: warp to the partner. F6 moves the local player beside the partner's reported position. The request
// waits on the simulation thread until it is safe and is refused while the player is dead, rides or drives a vehicle, is in a
// cutscene, a menu or a loading screen; a fall in progress delays it (the player must stand on something). Placing uses
// the engine's own teleport (a plain transform write on a player is undone by its mover), a little above the partner's
// height so a ground that streams in late is above the player, not under him (docs/DS2_NOTES.md, "Warp to partner").
#include "ds2/warp.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <mutex>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/place.h"
#include "ds2/remote_player.h"
#include "ds2/setdriver_guard.h"
#include "ds2/sim_tick.h"
#include "game.h"
#include "log.h"
#include "player_sync.h"

namespace {

constexpr int kWarpKey = VK_F6;
constexpr SHORT kKeyDown = static_cast<SHORT>(0x8000);
constexpr uintptr_t kGameStateGlobal = 0x14623E4C0, kGameStateBits = 0x160;
// DSGameState bits that refuse a warp: cutscenes (6, 7, 19, 20, 21, 22), the menus (8 weapon selector, 30 system pause),
// loading (24) and area change (31).
constexpr uint64_t kRefusingBits = (1ull << 6) | (1ull << 7) | (1ull << 8) | (1ull << 19) | (1ull << 20) | (1ull << 21) |
                                   (1ull << 22) | (1ull << 24) | (1ull << 30) | (1ull << 31);
constexpr double kBesideMetres = 2.0;       // the player lands this far to the partner's right
constexpr double kAboveMetres = 1.5;        // and this far above the partner's feet
constexpr double kFallSpeed = 8.0;          // metres per second downward: still falling, the warp waits
constexpr ULONGLONG kGiveUpMs = 15000;      // a warp that could not run in this long is dropped

struct Request {
    double x, y, z;
    float yaw;
    ULONGLONG since;
};

std::mutex g_mutex;
bool g_pending = false;
Request g_request{};

constexpr uintptr_t kEntityFlags = 0x98;
constexpr uint64_t kDeadFlag = uint64_t{1} << 8;  // Entity::IsDead

bool refused() {
    if (ds2::field<uint64_t>(remote_player::samEntity(), kEntityFlags) & kDeadFlag) return true;
    const uintptr_t state = decima::readPointer(ds2::at(kGameStateGlobal));
    if (!state || (ds2::field<uint64_t>(state, kGameStateBits) & kRefusingBits) != 0) return true;
    return game::drivenVehicle().has_value() || setdriver_guard::localPassengerVehicle() != 0;
}

// True while the local player is dropping fast.
bool falling() {
    static double lastZ = 0;
    static ULONGLONG lastAt = 0;
    decima::WorldTransform sam;
    const ULONGLONG now = GetTickCount64();
    if (!ds2::entityTransform(remote_player::samEntity(), sam)) return false;
    const bool fast = lastAt && now > lastAt && (lastZ - sam.position.z) / (static_cast<double>(now - lastAt) / 1000.0) > kFallSpeed;
    lastZ = sam.position.z;
    lastAt = now;
    return fast;
}

void tick() {
    Request request;
    {
        std::lock_guard lock(g_mutex);
        if (!g_pending) return;
        request = g_request;
    }
    const auto drop = [](const char* why) {
        std::lock_guard lock(g_mutex);
        g_pending = false;
        logger::write("warp: %s", why);
    };
    if (GetTickCount64() - request.since > kGiveUpMs) return drop("gave up waiting for a safe moment");
    if (refused()) return drop("refused: dead, riding, in a cutscene, a menu or a loading screen");
    if (falling()) return;
    decima::WorldTransform where{};
    if (!ds2::entityTransform(remote_player::samEntity(), where)) return drop("no player entity");
    const double right[2] = {std::cos(request.yaw), -std::sin(request.yaw)};
    where.position.x = request.x + right[0] * kBesideMetres;
    where.position.y = request.y + right[1] * kBesideMetres;
    where.position.z = request.z + kAboveMetres;
    if (!ds2::teleportEntity(remote_player::samEntity(), where)) return drop("the teleport faulted");
    logger::write("warp: placed beside the partner at %.1f %.1f %.1f", where.position.x, where.position.y, where.position.z);
    std::lock_guard lock(g_mutex);
    g_pending = false;
}

}  // namespace

namespace warp {

void installEarly() { sim_tick::add(&tick, "warp"); }

void poll() {
    static bool wasDown = false;
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    const bool down = pid == GetCurrentProcessId() && (GetAsyncKeyState(kWarpKey) & kKeyDown) != 0;
    const bool edge = down && !wasDown;
    wasDown = down;
    if (!edge) return;
    const auto peers = player_sync::remotePlayers();
    if (peers.empty()) {
        logger::write("warp: no partner");
        return;
    }
    const player_sync::RemotePlayer& peer = peers.front();
    std::lock_guard lock(g_mutex);
    g_request = {peer.position[0], peer.position[1], peer.position[2], peer.yaw, GetTickCount64()};
    g_pending = true;
    logger::write("warp: requested to %.1f %.1f %.1f", g_request.x, g_request.y, g_request.z);
}

}  // namespace warp
