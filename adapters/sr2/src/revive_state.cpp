#include "revive_state.h"

#include <mutex>

#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "remote_players.h"
#include "sr2_addresses.h"

namespace {

bool g_trace = false;
bool g_installed = false;
std::mutex g_lock;
revive_rules::RespawnWaits g_waits;

bool waitingOf(const partner_rules::Remote& remote, int32_t now) {
    if (g_installed && remote.slot < revive_rules::kWaitSlots) {
        std::lock_guard<std::mutex> lock(g_lock);
        return g_waits.waiting(remote.slot, now);
    }
    return revive_state::gameWaiting(remote.human);
}

}  // namespace

namespace revive_state {

void setTrace(bool trace) { g_trace = trace; }

bool trace() { return g_trace; }

void setInstalled(bool installed) { g_installed = installed; }

bool gameWaiting(uintptr_t human) {
    return (guest_call::call(sr2::kWaitingToRespawn, {.esi = static_cast<uint32_t>(human)}) & 0xFF) != 0;
}

int32_t now() { return static_cast<int32_t>(readDword(sr2::kGameClockMs)); }

size_t remotes(revive_rules::Player* out, size_t capacity) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    const int32_t clock = now();
    size_t count = 0;
    for (size_t i = 0; i < snapshot.pairing.count && count < capacity; ++i) {
        const partner_rules::Remote& remote = snapshot.pairing.items[i];
        if (!remote.human) continue;
        out[count++] = revive_rules::makePlayer(remote.human, remote.slot, readDword(remote.human + sr2::kHumanHealthOffset),
                                                remote_players::downedTimerOf(remote.human), waitingOf(remote, clock));
    }
    return count;
}

bool localDown() {
    const uintptr_t local = remote_players::localHuman();
    return local && remote_players::downedTimerOf(local) >= 0;
}

bool localWaiting() {
    const uintptr_t local = remote_players::localHuman();
    return local && gameWaiting(local);
}

void recordDeath(size_t slot) {
    const int32_t deadline = revive_rules::deadlineAfter(now(), sr2::kRespawnWaitMs);
    std::lock_guard<std::mutex> lock(g_lock);
    g_waits.record(slot, deadline);
}

void clearWaits() {
    std::lock_guard<std::mutex> lock(g_lock);
    g_waits.clear();
}

}  // namespace revive_state
