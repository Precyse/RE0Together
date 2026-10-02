#include "ds2/remote_appearance.h"

#include <atomic>
#include <cstdint>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/remote_player.h"
#include "hooks.h"

namespace {

constexpr uintptr_t kVariantLoaderTick = 0x140e61f40;  // (loader, dt)
constexpr uintptr_t kEntityVariantLoader = 0x5700;
constexpr uintptr_t kLoaderCurrentVariant = 0x6c;    // u8, the variant in use
constexpr uintptr_t kLoaderRequestedVariant = 0x6e;  // u8, the variant to load (9 = no request)

using TickFn = void (*)(uintptr_t loader, float dt);
TickFn g_tick = nullptr;
std::atomic<bool> g_requested{false};

void tickDetour(uintptr_t loader, float dt) {
    g_tick(loader, dt);
    const uintptr_t remote = remote_player::entity();
    if (!remote) return;
    const uintptr_t samLoader = decima::readPointer(remote_player::samEntity() + kEntityVariantLoader);
    const uintptr_t remoteLoader = decima::readPointer(remote + kEntityVariantLoader);
    if (!remoteLoader || loader != samLoader) return;
    if (!g_requested.exchange(true)) {
        const uint8_t variant = ds2::field<uint8_t>(samLoader, kLoaderCurrentVariant);
        ds2::field<uint8_t>(remoteLoader, kLoaderRequestedVariant) = variant;
    }
    g_tick(remoteLoader, dt);
}

}  // namespace

namespace remote_appearance {

void installEarly() {
    hooks::install("body variant tick", ds2::at(kVariantLoaderTick), reinterpret_cast<void*>(&tickDetour),
                   reinterpret_cast<void**>(&g_tick));
}

void onSpawned() { g_requested = false; }

}  // namespace remote_appearance
