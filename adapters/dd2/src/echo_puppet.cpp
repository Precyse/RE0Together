#include "echo_puppet.h"

#include <cmath>
#include <optional>
#include <string>

#include "calls.h"
#include "local_body.h"
#include "log.h"
#include "look.h"
#include "puppet_builder.h"
#include "refw.h"
#include "wire_dd2.h"
#include "world_gate.h"

namespace {

constexpr uint64_t kSettleMs = 10000;  // plain gameplay this long before the probe runs
constexpr double kAheadM = 3.0;
constexpr uint64_t kProbeLifeMs = 60000;  // the probe's copy is removed again after this

bool g_enabled = false;
bool g_done = false;
std::optional<uint64_t> g_plainSinceMs;
std::optional<puppet_builder::Puppet> g_puppet;
uint64_t g_builtMs = 0;

void run(uint64_t nowMs) {
    g_done = true;
    const auto at = local_body::sample(wire_dd2::kKindArisen);
    const auto contexts = at ? look::of(local_body::character(wire_dd2::kKindArisen)) : std::nullopt;
    if (!contexts) return void(logger::write("echo_puppet: stopped at sampling the Arisen"));
    for (size_t i = 0; i < look::kCount; ++i) {
        logger::write("echo_puppet: %s %s", look::kSlots[i].type, (*contexts)[i] ? "copied" : "missing");
    }
    // Through JSON and back, as a peer's look arrives (peer_puppet.h): one PC tests the whole route.
    look::Json json;
    look::Objects rebuilt{};
    if (!look::toJson(*contexts, json)) return void(logger::write("echo_puppet: stopped at the JSON serializer"));
    size_t bytes = 0;
    for (const std::string& text : json) bytes += text.size();
    logger::write("echo_puppet: look as JSON, %zu bytes", bytes);
    if (!look::fromJson(json, rebuilt)) return void(logger::write("echo_puppet: stopped at the JSON deserializer"));
    void* writer = look::writer(rebuilt);
    look::release(rebuilt);
    if (!writer) return void(logger::write("echo_puppet: stopped at the writer"));
    const double position[3] = {at->position[0] + std::sin(at->yaw) * kAheadM, at->position[1],
                                at->position[2] + std::cos(at->yaw) * kAheadM};
    g_puppet = puppet_builder::create(writer);
    refw::release(writer);
    if (g_puppet && !puppet_builder::generate(*g_puppet, position, at->yaw)) {
        puppet_builder::release(*g_puppet);
        g_puppet.reset();
    }
    g_builtMs = nowMs;
}

}  // namespace

namespace echo_puppet {

void setEnabled(bool on) { g_enabled = on; }

void tick(uint64_t nowMs) {
    if (g_puppet && nowMs - g_builtMs >= kProbeLifeMs) {
        puppet_builder::despawn(*g_puppet);
        puppet_builder::release(*g_puppet);
        g_puppet.reset();
    }
    if (!g_enabled || g_done || !calls::enabled(calls::Feature::Puppet) || !calls::enabled(calls::Feature::Body)) return;
    if (world_gate::place() != world_gate_rule::Place::InGame) {
        g_plainSinceMs.reset();
        return;
    }
    if (!g_plainSinceMs) g_plainSinceMs = nowMs;
    if (nowMs - *g_plainSinceMs >= kSettleMs) run(nowMs);
}

}  // namespace echo_puppet
