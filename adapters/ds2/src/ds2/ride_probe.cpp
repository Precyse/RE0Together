// DEATH STRANDING 2: log-only probe of the ride plugin's confirm (see ds2/ride_probe.h).
#include "ds2/ride_probe.h"

#include <cstdint>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/player_state.h"
#include "ds2/remote_player.h"
#include "ds2/ride_plugin.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kTryStart = 0x1410047d0;    // (plugin) -> bool: starts a ride from the owner's request or the prompt
constexpr uintptr_t kPromptGate = 0x141011b20;  // (plugin) -> bool: the prompt path's first check; false ends TryStart
constexpr uintptr_t kGlobalBlock = 0x14623E9C8;  // pointer to the block whose byte +0x2264bb makes TryStart return false
constexpr uintptr_t kGlobalBlockFlag = 0x2264bb;
constexpr uintptr_t kPluginRequestPath = 0x24498;   // set by the request path
constexpr uintptr_t kPluginReach = 0x24600;         // float: the prompt path needs it above 0
constexpr uintptr_t kPluginDoorBlocker = 0x245f2;
constexpr uintptr_t kPluginSeatBlocker = 0x245f3;
constexpr uintptr_t kPluginPromptTarget = 0x245f8;  // the vehicle id the prompt path starts

// What the probe saw of one TryStart call; a line is written only when it differs from the last one.
struct Sample {
    bool started = false;
    bool gate = false;
    bool globalBlock = false;
    uint8_t requestKind = 0;
    uint8_t phase = 0;
    uint16_t requestedPhase = 0;
    uint8_t requestPath = 0;
    float reach = 0;
    uint8_t doorBlocker = 0;
    uint8_t seatBlocker = 0;
    uint64_t target = 0;
    uint64_t promptTarget = 0;

    bool operator==(const Sample&) const = default;
};

using PluginFn = bool (*)(uintptr_t plugin);
PluginFn g_tryStart = nullptr;
PluginFn g_promptGate = nullptr;
bool g_lastGate = false;  // the prompt gate's answer in the TryStart call being run

bool isLocalPlugin(uintptr_t plugin) { return plugin && plugin == ds2::ridePlugin(remote_player::samEntity()); }

bool promptGateDetour(uintptr_t plugin) {
    const bool answer = g_promptGate(plugin);
    if (isLocalPlugin(plugin)) g_lastGate = answer;
    return answer;
}

Sample sampleOf(uintptr_t plugin, bool started) {
    const uintptr_t owner = decima::readPointer(plugin + ride_plugin::kOwner);
    const uintptr_t block = decima::readPointer(ds2::at(kGlobalBlock));
    Sample sample;
    sample.started = started;
    sample.gate = g_lastGate;
    sample.globalBlock = block && ds2::field<uint8_t>(block, kGlobalBlockFlag) != 0;
    sample.requestKind = owner ? ds2::field<uint8_t>(owner, ride_plugin::kOwnerRequestKind) : 0;
    sample.phase = ds2::field<uint8_t>(plugin, ride_plugin::kPhase);
    sample.requestedPhase = ds2::field<uint16_t>(plugin, ride_plugin::kRequestedPhase);
    sample.requestPath = ds2::field<uint8_t>(plugin, kPluginRequestPath);
    sample.reach = ds2::field<float>(plugin, kPluginReach);
    sample.doorBlocker = ds2::field<uint8_t>(plugin, kPluginDoorBlocker);
    sample.seatBlocker = ds2::field<uint8_t>(plugin, kPluginSeatBlocker);
    sample.target = ds2::field<uint64_t>(plugin, ride_plugin::kTarget);
    sample.promptTarget = ds2::field<uint64_t>(plugin, kPluginPromptTarget);
    return sample;
}

bool tryStartDetour(uintptr_t plugin) {
    const bool local = isLocalPlugin(plugin);
    if (local) g_lastGate = false;
    const bool started = g_tryStart(plugin);
    if (!local) return started;
    static Sample last;
    const Sample now = sampleOf(plugin, started);
    if (!(now == last)) {
        last = now;
        logger::write("ride_probe: TryStart %d, prompt gate %d, global block %d, request kind %u, phase %u, requested %04x, "
                      "request path %u, reach %.2f, door blocker %u, seat blocker %u, target %llx, prompt target %llx",
                      now.started, now.gate, now.globalBlock, now.requestKind, now.phase, now.requestedPhase, now.requestPath,
                      now.reach, now.doorBlocker, now.seatBlocker, static_cast<unsigned long long>(now.target),
                      static_cast<unsigned long long>(now.promptTarget));
    }
    return started;
}

}  // namespace

namespace ride_probe {

void installEarly() {
    hooks::install("ride prompt gate", ds2::at(kPromptGate), reinterpret_cast<void*>(&promptGateDetour),
                   reinterpret_cast<void**>(&g_promptGate));
    hooks::install("ride try start", ds2::at(kTryStart), reinterpret_cast<void*>(&tryStartDetour),
                   reinterpret_cast<void**>(&g_tryStart));
}

}  // namespace ride_probe
