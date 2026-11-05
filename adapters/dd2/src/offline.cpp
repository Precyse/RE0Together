#include "offline.h"

#include "calls.h"
#include "log.h"
#include "refw.h"
#include "session.h"
#include "world_gate.h"

namespace {

constexpr const char* kNetworkManager = "app.NetworkManager";

bool g_requested = false;

}  // namespace

namespace offline {

void tick(uint64_t) {
    if (!session::active()) {
        g_requested = false;  // a new session asks again
        return;
    }
    if (g_requested || !calls::enabled(calls::Feature::Offline)) return;
    if (world_gate::place() == world_gate_rule::Place::Title) return;  // the title runs its own online checks
    void* manager = refw::managedSingleton(kNetworkManager);
    if (!manager) return;
    g_requested = true;
    // No success or error callbacks: the engine keeps the result in its own online state.
    const bool ok = refw::invoke(calls::method(calls::kRequestForceOfflinePlay), manager, {nullptr, nullptr}) != nullptr;
    logger::write("offline: forced offline play requested (%s)", ok ? "ok" : "call failed");
}

}  // namespace offline
