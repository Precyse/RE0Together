#include "coop_scene.h"

#include <chrono>
#include <mutex>

#include "coop_scene_rule.h"

namespace {

using Clock = std::chrono::steady_clock;

std::mutex g_mutex;
coop_scene::Scene g_scene;  // guarded by g_mutex
Clock::time_point g_published;

}  // namespace

namespace coop_scene {

void publish(const Scene& scene) {
    std::lock_guard lock(g_mutex);
    g_scene = scene;
    g_published = Clock::now();
}

std::optional<Scene> current() {
    std::lock_guard lock(g_mutex);
    const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_published).count();
    if (g_published == Clock::time_point{} || !coop_scene_rule::fresh(static_cast<uint64_t>(age))) return std::nullopt;
    return g_scene;
}

}  // namespace coop_scene
