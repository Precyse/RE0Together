// DEATH STRANDING 2: the rack and equipment mirrors delete pieces of the remote body's owner from the net thread; the game
// updates that owner (and the player entity that holds it) on the simulation thread, and a burst of deletions while it does
// crashed its equipment code (mass removal after a cargo theft: file va 0x140F6FD25). Deletions are queued here and run on
// the simulation tick, ahead of the engine's object update, a few per frame (docs/DS2_NOTES.md, "Cargo mass removal").
#include "ds2/cargo_defer.h"

#include <mutex>
#include <vector>

#include "ds2/sim_tick.h"
#include "game.h"

namespace {

constexpr size_t kPerFrame = 3;
constexpr size_t kMaxQueued = 512;

std::mutex g_mutex;
std::vector<uint64_t> g_handles;

void tick() {
    std::vector<uint64_t> now;
    {
        std::lock_guard lock(g_mutex);
        const size_t count = std::min(kPerFrame, g_handles.size());
        now.assign(g_handles.begin(), g_handles.begin() + count);
        g_handles.erase(g_handles.begin(), g_handles.begin() + count);
    }
    for (const uint64_t handle : now) game::removeCargo(handle);
}

}  // namespace

namespace cargo_defer {

void installEarly() { sim_tick::add(&tick, "cargo deletions"); }

}  // namespace cargo_defer

namespace game {

void removeCargoLater(uint64_t handle) {
    std::lock_guard lock(g_mutex);
    if (g_handles.size() < kMaxQueued) g_handles.push_back(handle);
}

}  // namespace game
