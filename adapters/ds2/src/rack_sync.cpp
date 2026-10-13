#include "rack_sync.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

#include "cargo_transfer.h"
#include "ds2/sim_tick.h"
#include "game.h"
#include "log.h"
#include "partner_cargo_sync.h"
#include "remote_body.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kCheckInterval = std::chrono::milliseconds(500);
constexpr int kMaxChangesPerRound = 6;  // a burst (a cargo theft on the partner) is spread over several rounds
constexpr auto kSettle = std::chrono::seconds(2);  // the game serves create and delete requests on its next update

// A piece is told apart by its kind and its order id (0 for plain cargo): two pieces of one order never share an id.
using PieceKey = std::pair<uint32_t, uint64_t>;
using Wanted = std::map<PieceKey, std::vector<game::Cargo>>;

// What the net thread hands the simulation thread: the body, and, when the partner's body is this body, the partner's reported
// rack with this machine's moves the partner has not acknowledged yet.
struct Target {
    std::optional<uint64_t> body;
    std::optional<std::vector<game::Cargo>> reported;
    std::vector<game::Cargo> left, given;
};

std::mutex g_mutex;
Target g_target;  // net thread writes, simulation thread reads (guarded by g_mutex)

// Simulation thread only.
Clock::time_point g_lastCheck;
Clock::time_point g_lastChange;
std::map<PieceKey, size_t> g_lastWanted;
std::set<PieceKey> g_refused;  // pieces the game refused to create (this world holds the same order piece): not asked again until the target changes

PieceKey keyOf(const game::Cargo& piece) { return {piece.type, piece.orderId}; }

// The partner's rack as the body should hold it: what the partner reported, less the pieces the body just lost to this
// world (the partner deletes its copy a moment later, and its list may still show it), plus the ones this world just put
// into the body (the partner creates its copy a moment later, and its list may not show it yet).
Wanted wantedRack(const Target& target, const std::vector<game::Cargo>& have) {
    Wanted want;
    for (const game::Cargo& piece : *target.reported) want[keyOf(piece)].push_back(piece);
    for (const game::Cargo& gone : target.left) {
        if (auto it = want.find(keyOf(gone)); it != want.end() && !it->second.empty()) it->second.pop_back();
    }
    for (const game::Cargo& given : target.given) {
        const bool held = std::any_of(have.begin(), have.end(), [&](const auto& piece) { return keyOf(piece) == keyOf(given); });
        if (held && want[keyOf(given)].empty()) want[keyOf(given)].push_back(given);
    }
    return want;
}

std::map<PieceKey, size_t> counts(const Wanted& want) {
    std::map<PieceKey, size_t> out;
    for (const auto& [key, pieces] : want) {
        if (!pieces.empty()) out[key] = pieces.size();
    }
    return out;
}

// Brings the body's backpack to the partner's pieces: extra pieces deleted, missing ones created with their order link.
void follow(uint64_t ownerKey, const Target& target, Clock::time_point now) {
    const std::vector<game::Cargo> present = game::backpackCargo(ownerKey);
    const Wanted want = wantedRack(target, present);
    // Only a target that has not changed since the previous check is applied (see equip_sync).
    const std::map<PieceKey, size_t> wantCounts = counts(want);
    const bool steady = wantCounts == g_lastWanted;
    if (!steady) g_refused.clear();
    g_lastWanted = wantCounts;
    if (!steady) return;
    std::map<PieceKey, std::vector<uint64_t>> have;
    for (const game::Cargo& piece : present) have[keyOf(piece)].push_back(piece.handle);
    bool same = true;
    for (const auto& [key, count] : wantCounts) same = same && (have[key].size() == count || g_refused.contains(key));
    for (const auto& [key, handles] : have) same = same && wantCounts.contains(key) && wantCounts.at(key) == handles.size();
    if (same || now - g_lastChange < kSettle) return;
    g_lastChange = now;
    int removed = 0, added = 0;
    for (const auto& [key, handles] : have) {
        const size_t keep = wantCounts.contains(key) ? wantCounts.at(key) : 0;
        for (size_t i = keep; i < handles.size() && removed + added < kMaxChangesPerRound; ++i) {
            game::removeCargoLater(handles[i]);
            ++removed;
        }
    }
    for (const auto& [key, pieces] : want) {
        for (size_t i = have[key].size(); i < pieces.size() && !g_refused.contains(key) && removed + added < kMaxChangesPerRound; ++i) {
            const game::AddResult result = game::addBackpackCargo(ownerKey, pieces[i]);
            added += result == game::AddResult::Done ? 1 : 0;
            if (result == game::AddResult::Refused) g_refused.insert(key);
        }
    }
    logger::write("rack_sync: the body's rack: %d pieces removed, %d added (%zu reported)", removed, added, target.reported->size());
}

void simTick() {
    const auto now = Clock::now();
    if (now - g_lastCheck < kCheckInterval) return;
    g_lastCheck = now;
    Target target;
    {
        std::lock_guard lock(g_mutex);
        target = g_target;
    }
    if (target.body) game::setOwnerActive(*target.body, true);  // the cargo menus skip an inactive owner; the game may switch it off again
    if (target.body && target.reported) follow(*target.body, target, now);
}

}  // namespace

namespace rack_sync {

void installEarly() { sim_tick::add(&simTick, "rack sync", sim_tick::Gate::Gameplay); }

void tick(NetClient&, const SessionSnapshot& session) {
    Target target;
    if (session.linked) {
        target.body = remote_body::ownerKey();
        const auto partner = cargo_transfer::partner();
        if (target.body && partner && partner->slot == remote_body::slot()) {
            target.reported = partner->cargo;
            target.left = partner_cargo_sync::recentlyLeft();
            target.given = partner_cargo_sync::recentlyGiven();
        }
    }
    std::lock_guard lock(g_mutex);
    g_target = std::move(target);
}

}  // namespace rack_sync
