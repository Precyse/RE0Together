#include "equip_sync.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "ds2/sim_tick.h"
#include "game.h"
#include "log.h"
#include "remote_body.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kCheckInterval = std::chrono::milliseconds(500);
constexpr int kMaxChangesPerRound = 6;  // a burst (a cargo theft on the partner) is spread over several rounds
constexpr auto kSettle = std::chrono::seconds(2);  // the game serves create and delete requests on its next update
// A request the game has not served is never repeated (pieces requested twice are both made, and the surplus lies on
// the ground); it is only forgotten after this, in case the game dropped it.
constexpr auto kRequestBackstop = std::chrono::seconds(120);

// What the simulation thread hands to the net thread and back (engine data is only read on the simulation thread).
std::mutex g_mutex;
std::vector<equip_sync::Held> g_localHeld;                    // the local player's pieces, last read
std::map<uint8_t, std::vector<equip_sync::Held>> g_peerHeld;  // by source slot

// Net thread only.
std::vector<equip_sync::Held> g_reported;

// Simulation thread only: the pieces of one body. `ours` is the ledger of the handles we created for it; nothing else in
// its slots is ever deleted (the pieces it is born with stay: deleting one crashed the player entity's equipment code).
struct Pending {
    int count = 0;  // creations asked for and not seen in the slot yet
    Clock::time_point since;
};
struct Body {
    uint64_t owner = 0;
    std::set<uint64_t> seen;  // every handle seen in its slots so far
    std::set<uint64_t> ours;
    std::map<std::pair<uint8_t, uint32_t>, Pending> pending;
    std::vector<equip_sync::Held> lastWanted;
    std::vector<equip_sync::Held> lastOwn;  // its slots at the previous check: the body is left alone until they stop changing
    Clock::time_point lastChange;
    bool awaitingMatch = false;  // follow changed the slots and they have not matched the partner's since
};
Body g_body;
Clock::time_point g_lastCheck;
uint32_t g_sweptEpoch = 0;

bool lessHeld(const equip_sync::Held& a, const equip_sync::Held& b) {
    return a.slot != b.slot ? a.slot < b.slot : a.type < b.type;
}

// What an owner holds in the mirrored slots, one pass over the engine's pool.
std::vector<game::SlotPiece> piecesOf(uint64_t ownerKey) {
    return game::slotPiecesOfKinds(ownerKey, equip_sync::kMirroredSlots, std::size(equip_sync::kMirroredSlots));
}

std::vector<equip_sync::Held> heldIn(const std::vector<game::SlotPiece>& pieces) {
    std::vector<equip_sync::Held> held;
    for (const game::SlotPiece& piece : pieces) held.push_back({piece.slot, {}, piece.type});
    std::sort(held.begin(), held.end(), lessHeld);
    held.resize(std::min<size_t>(held.size(), equip_sync::kMaxHeld));
    return held;
}

// The pieces by slot, for the log: "slot:kind" per piece.
std::string describe(std::vector<game::SlotPiece> pieces) {
    std::sort(pieces.begin(), pieces.end(), [](const auto& a, const auto& b) { return a.slot < b.slot; });
    std::string text;
    for (const game::SlotPiece& piece : pieces) text += " " + std::to_string(piece.slot) + ":" + std::to_string(piece.type);
    return text.empty() ? " none" : text;
}

// Deletes the body's pieces that a load dropped on the ground: they carry the mark the body's pieces get (game.h). The
// pieces of a world being torn down look loose for a moment too, so the sweep runs once per world, once it is built and
// before the body is (a body being built while its dropped pieces are deleted crashed the equipment code).
void removeOrphansOncePerWorld() {
    if (!sim_tick::worldBuilt() || g_sweptEpoch == sim_tick::gameplayEpoch()) return;
    g_sweptEpoch = sim_tick::gameplayEpoch();
    for (const uint64_t handle : game::markedLooseCargo()) {
        game::removeCargoLater(handle);
        logger::write("equip_sync: removed a piece of the body's dropped by a load (%llx)", static_cast<unsigned long long>(handle));
    }
}

// Files the handles that appeared in the body's slots since the last check: one we asked for is ours, any other is
// the body's own (born with it).
void recordArrivals(const std::vector<game::SlotPiece>& pieces, Clock::time_point now) {
    for (const game::SlotPiece& piece : pieces) {
        if (!g_body.seen.insert(piece.handle).second) continue;
        const auto asked = g_body.pending.find({piece.slot, piece.type});
        if (asked == g_body.pending.end() || asked->second.count == 0) continue;
        g_body.ours.insert(piece.handle);
        if (--asked->second.count == 0) g_body.pending.erase(asked);
    }
    std::erase_if(g_body.pending, [&](const auto& entry) { return now - entry.second.since > kRequestBackstop; });
}

// Brings the body's slots to what its partner carries: pieces of ours the partner no longer carries are deleted, kinds
// it lacks are created. Requests the game has not served yet count as done.
void follow(uint64_t ownerKey, const std::vector<game::SlotPiece>& pieces, const std::vector<equip_sync::Held>& wanted,
            Clock::time_point now) {
    // Only a target that has not changed since the previous check is applied: a flapping partner would otherwise
    // create and delete pieces in the body's slots every few hundred milliseconds.
    const bool steady = wanted == g_body.lastWanted;
    g_body.lastWanted = wanted;
    recordArrivals(pieces, now);
    if (!steady || now - g_body.lastChange < kSettle) return;
    if (heldIn(pieces) == wanted) {
        g_body.pending.clear();
        if (g_body.awaitingMatch) logger::write("equip_sync: the body's slots now match the partner's:%s", describe(pieces).c_str());
        g_body.awaitingMatch = false;
        return;
    }
    const std::string before = describe(pieces);
    int changes = 0;
    std::string plan;
    for (const uint8_t slot : equip_sync::kMirroredSlots) {
        std::multiset<uint32_t> missing;  // the kinds the partner has in this slot that the body does not yet
        for (const equip_sync::Held& want : wanted) {
            if (want.slot == slot) missing.insert(want.type);
        }
        for (const game::SlotPiece& piece : pieces) {
            if (piece.slot != slot) continue;
            const auto match = missing.find(piece.type);
            if (match != missing.end()) {
                missing.erase(match);
            } else if (changes < kMaxChangesPerRound && g_body.ours.contains(piece.handle)) {
                game::removeCargoLater(piece.handle);
                g_body.ours.erase(piece.handle);
                plan += " -" + std::to_string(slot) + ":" + std::to_string(piece.type);
                ++changes;
            }
        }
        for (const uint32_t type : std::set<uint32_t>(missing.begin(), missing.end())) {
            const auto asked = g_body.pending.find({slot, type});
            const int onTheWay = asked == g_body.pending.end() ? 0 : asked->second.count;
            for (int i = static_cast<int>(missing.count(type)) - onTheWay; i > 0 && changes < kMaxChangesPerRound; --i) {
                if (!game::addSlotPiece(ownerKey, slot, type)) continue;
                Pending& pending = g_body.pending[{slot, type}];
                if (pending.count++ == 0) pending.since = now;
                plan += " +" + std::to_string(slot) + ":" + std::to_string(type);
                ++changes;
            }
        }
    }
    if (!changes) return;
    g_body.lastChange = now;
    g_body.awaitingMatch = true;
    logger::write("equip_sync: the body's slots now follow %zu carried pieces (%d changes:%s), before:%s", wanted.size(), changes,
                  plan.c_str(), before.c_str());
}

// The simulation thread, every check interval while a world is up.
void simTick() {
    const auto now = Clock::now();
    if (now - g_lastCheck < kCheckInterval) return;
    g_lastCheck = now;
    const std::vector<equip_sync::Held> local = heldIn(piecesOf(0));
    {
        std::lock_guard lock(g_mutex);
        g_localHeld = local;
    }
    removeOrphansOncePerWorld();
    const std::optional<uint64_t> owner = remote_body::ownerKey();
    if (!owner) {
        g_body = {};  // the next body starts from a clean ledger
        return;
    }
    if (local.empty()) return;  // the local player's own slots empty out as a load starts: the world is going away
    if (g_body.owner != *owner) {
        g_body = {};
        g_body.owner = *owner;
        for (const game::SlotPiece& piece : piecesOf(*owner)) g_body.seen.insert(piece.handle);
    }
    const std::vector<game::SlotPiece> pieces = piecesOf(*owner);
    const std::vector<equip_sync::Held> own = heldIn(pieces);
    const bool built = own == g_body.lastOwn;  // the engine is still building the body's equipment while its slots change
    g_body.lastOwn = own;
    if (!built) return;
    game::markOwnedCargo(*owner);
    std::vector<equip_sync::Held> wanted;
    {
        std::lock_guard lock(g_mutex);
        const auto peer = g_peerHeld.find(remote_body::slot());
        if (peer == g_peerHeld.end()) return;
        wanted = peer->second;
    }
    follow(*owner, pieces, wanted, now);
}

}  // namespace

namespace equip_sync {

void installEarly() { sim_tick::add(&simTick, "equip sync", sim_tick::Gate::Gameplay); }

void onFrame(const GameFrame& frame) {
    EquipHeader header;
    if (frame.type != kMsgEquipState || frame.payload.size() < sizeof(header)) return;
    std::memcpy(&header, frame.payload.data(), sizeof(header));
    if (header.count > kMaxHeld || frame.payload.size() != sizeof(header) + header.count * sizeof(Held)) return;
    std::vector<Held> held(header.count);
    if (header.count) std::memcpy(held.data(), frame.payload.data() + sizeof(header), header.count * sizeof(Held));
    std::sort(held.begin(), held.end(), lessHeld);
    std::lock_guard lock(g_mutex);
    g_peerHeld[frame.slot] = std::move(held);
}

void tick(NetClient& net, const SessionSnapshot& session) {
    if (!session.linked) return;
    std::vector<Held> held;
    {
        std::lock_guard lock(g_mutex);
        held = g_localHeld;
    }
    if (held == g_reported) return;
    const EquipHeader header{static_cast<uint32_t>(held.size()), 0};
    std::vector<uint8_t> payload(sizeof(header) + held.size() * sizeof(Held));
    std::memcpy(payload.data(), &header, sizeof(header));
    if (!held.empty()) std::memcpy(payload.data() + sizeof(header), held.data(), held.size() * sizeof(Held));
    if (!net.send(kMsgEquipState, true, proto::kSlotAll, payload)) return;
    g_reported = std::move(held);
    logger::write("equip_sync: reported %zu carried pieces", g_reported.size());
}

}  // namespace equip_sync
