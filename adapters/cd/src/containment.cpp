#include "containment.h"

#include <atomic>

#include "build_gate.h"
#include "cd_sites.h"
#include "hooks.h"
#include "log.h"
#include "safe_read.h"
#include "template_rule.h"

namespace {

// Forwarded with eight integer arguments, as the funnels are: the real ones plus caller stack slots the callee ignores.
using Update = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

constexpr uint32_t kControlActor = 0x08;
constexpr uint32_t kActorKey = 0x60;
constexpr uint32_t kLayers[] = {0x88, 0x90};  // the two action layers
constexpr uint32_t kLayerCurrent = 0x10;
constexpr uint32_t kLayerPlayback = 0x18;
constexpr uint32_t kLayerChart = 0x110;

Update g_update = nullptr, g_postUpdate = nullptr;
constexpr uint8_t kJumpOpcode = 0xE9;  // what an enabled hook leaves at the site
bool g_installed = false;
bool g_active = false;  // installed and seen enabled after the batch
std::atomic<uint64_t> g_skipped{0};

// A copy with a layer that has a chart but nothing playing.
bool mustSkip(uint64_t control) {
    const uintptr_t actor = safe_read::pointer(control + kControlActor);
    uint32_t key = 0;
    if (!actor || !safe_read::value(actor + kActorKey, key) || !template_rule::isCopyKey(key)) return false;
    for (uint32_t layerAt : kLayers) {
        const uintptr_t layer = safe_read::pointer(control + layerAt);
        if (layer && safe_read::pointer(layer + kLayerChart) &&
            (!safe_read::pointer(layer + kLayerCurrent) || !safe_read::pointer(layer + kLayerPlayback))) {
            return true;
        }
    }
    return false;
}

uint64_t __fastcall updateDetour(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7,
                                 uint64_t a8) {
    if (mustSkip(a1)) {
        g_skipped.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    return g_update(a1, a2, a3, a4, a5, a6, a7, a8);
}

uint64_t __fastcall postUpdateDetour(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6,
                                     uint64_t a7, uint64_t a8) {
    if (mustSkip(a1)) {
        g_skipped.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    return g_postUpdate(a1, a2, a3, a4, a5, a6, a7, a8);
}

}  // namespace

namespace containment {

bool install() {
    if (!build_gate::setIntact(cd_sites::kContainment)) {
        logger::write("containment: not patched (site bytes differ)");
        return false;
    }
    const bool update = hooks::install(cd_sites::kCcUpdate.name, cd_sites::kCcUpdate.address,
                                       reinterpret_cast<void*>(&updateDetour), reinterpret_cast<void**>(&g_update));
    const bool post = update && hooks::install(cd_sites::kCcPostUpdate.name, cd_sites::kCcPostUpdate.address,
                                               reinterpret_cast<void*>(&postUpdateDetour),
                                               reinterpret_cast<void**>(&g_postUpdate));
    if (update && !post) hooks::remove(cd_sites::kCcUpdate.address);
    g_installed = update && post;
    logger::write(g_installed ? "containment: 2 patched" : "containment: not patched (hook install failed)");
    return g_installed;
}

bool confirmEnabled() {
    uint8_t update = 0, post = 0;
    g_active = g_installed && safe_read::value(cd_sites::kCcUpdate.address, update) && update == kJumpOpcode &&
               safe_read::value(cd_sites::kCcPostUpdate.address, post) && post == kJumpOpcode;
    if (g_installed && !g_active) logger::write("containment: hooks not enabled after the batch: copies stay off");
    return g_active;
}

bool active() { return g_active; }

uint64_t skipped() { return g_skipped.load(std::memory_order_relaxed); }

}  // namespace containment
