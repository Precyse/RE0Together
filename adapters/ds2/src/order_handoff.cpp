#include "order_handoff.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <vector>

#include "cargo_transfer.h"
#include "ds2/sim_tick.h"
#include "game.h"
#include "log.h"
#include "remote_body.h"
#include "story_replay.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kHandOffWindow = std::chrono::seconds(30);
constexpr auto kScanInterval = std::chrono::milliseconds(500);

struct Pending {
    uint64_t missionId;
    Clock::time_point since;
    size_t seen = 0;  // pieces found by the previous scan: the cargo is given once two scans agree
};

std::mutex g_mutex;
std::vector<Pending> g_pending;      // guarded by g_mutex: written by expect and the scan
std::vector<game::Cargo> g_toGive;   // guarded by g_mutex: found by the scan, given by tick
Clock::time_point g_lastScan;        // simulation thread only

// Whether the pending order's cargo is complete: the mission runs and the same non-empty set of pieces was seen twice.
bool ready(Pending& pending, std::vector<game::Cargo>& pieces) {
    const auto state = game::missionState(pending.missionId);
    if (!state || *state != story_replay::kStateProgress) return false;
    pieces = game::orderPieces(pending.missionId, remote_body::ownerKey());
    const bool stable = !pieces.empty() && pieces.size() == pending.seen;
    pending.seen = pieces.size();
    return stable;
}

void scan() {
    const auto now = Clock::now();
    if (now - g_lastScan < kScanInterval) return;
    g_lastScan = now;
    std::lock_guard lock(g_mutex);
    std::erase_if(g_pending, [&](Pending& pending) {
        std::vector<game::Cargo> pieces;
        if (ready(pending, pieces)) {
            g_toGive.insert(g_toGive.end(), pieces.begin(), pieces.end());
            return true;
        }
        if (now - pending.since < kHandOffWindow) return false;
        logger::write("order_handoff: order %llx has no cargo to hand over", static_cast<unsigned long long>(pending.missionId));
        return true;
    });
}

}  // namespace

namespace order_handoff {

void installEarly() { sim_tick::add(&scan, "order handoff", sim_tick::Gate::Gameplay); }

void expect(uint64_t missionId) {
    std::lock_guard lock(g_mutex);
    const bool known = std::any_of(g_pending.begin(), g_pending.end(), [&](const Pending& p) { return p.missionId == missionId; });
    if (!known) g_pending.push_back({missionId, Clock::now()});
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const uint8_t guest = remote_body::slot();
    std::vector<game::Cargo> pieces;
    {
        std::lock_guard lock(g_mutex);
        if (!session.linked || session.localSlot != session.hostSlot) {
            g_pending.clear();
            g_toGive.clear();
            return;
        }
        if (guest == proto::kSlotAll || !game::gameplaySettled()) return;
        pieces.swap(g_toGive);
    }
    for (const game::Cargo& piece : pieces) {
        const bool given = cargo_transfer::handOver(net, guest, piece);
        logger::write("order_handoff: order %llx piece %u %s", static_cast<unsigned long long>(piece.orderId), piece.type,
                      given ? "given to the guest" : "not given");
    }
}

}  // namespace order_handoff
