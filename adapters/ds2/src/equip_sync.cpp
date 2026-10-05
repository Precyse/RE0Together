#include "equip_sync.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
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
// Net thread only.
Clock::time_point g_lastCheck;
Clock::time_point g_lastChange;
std::vector<equip_sync::Held> g_reported;
std::vector<equip_sync::Held> g_lastWanted;

// Creations asked for and not seen in the body's slots yet, by slot and kind: `baseline` pieces of that kind were
// there when the first was asked for, so `requested - (seen - baseline)` of them are still on their way.
struct Requested {
    int count = 0;
    int baseline = 0;
    Clock::time_point since;
};
std::map<std::pair<uint8_t, uint32_t>, Requested> g_requestedAdds;
std::set<uint64_t> g_requestedDeletes;  // handles whose deletion was asked for and that are still in the slots
uint64_t g_protectedOwner = 0;
std::set<uint64_t> g_protected;  // handles of the pieces the body was born with
std::map<uint8_t, std::vector<equip_sync::Held>> g_peerHeld;  // by source slot
bool g_awaitingMatch = false;      // follow changed the body's slots and they have not matched the partner's since

bool lessHeld(const equip_sync::Held& a, const equip_sync::Held& b) {
    return a.slot != b.slot ? a.slot < b.slot : a.type < b.type;
}

std::vector<equip_sync::Held> heldBy(uint64_t ownerKey) {
    std::vector<equip_sync::Held> held;
    for (const uint8_t slot : equip_sync::kMirroredSlots) {
        for (const game::Cargo& piece : game::slotPieces(ownerKey, slot)) held.push_back({slot, {}, piece.type});
    }
    std::sort(held.begin(), held.end(), lessHeld);
    held.resize(std::min<size_t>(held.size(), equip_sync::kMaxHeld));
    return held;
}

void report(NetClient& net, const std::vector<equip_sync::Held>& held) {
    if (held == g_reported) return;
    const equip_sync::EquipHeader header{static_cast<uint32_t>(held.size()), 0};
    std::vector<uint8_t> payload(sizeof(header) + held.size() * sizeof(equip_sync::Held));
    std::memcpy(payload.data(), &header, sizeof(header));
    if (!held.empty()) std::memcpy(payload.data() + sizeof(header), held.data(), held.size() * sizeof(equip_sync::Held));
    if (!net.send(equip_sync::kMsgEquipState, true, proto::kSlotAll, payload)) return;
    g_reported = held;
    logger::write("equip_sync: reported %zu carried pieces", held.size());
}

bool isGear(uint8_t slot) { return std::find(std::begin(equip_sync::kGearSlots), std::end(equip_sync::kGearSlots), slot) != std::end(equip_sync::kGearSlots); }

int seenCount(const std::vector<game::Cargo>& pieces, uint32_t type) {
    return static_cast<int>(std::count_if(pieces.begin(), pieces.end(), [&](const game::Cargo& piece) { return piece.type == type; }));
}

// Creations still on their way for this slot and kind (and forgets the ones that arrived or went stale).
int outstandingAdds(uint8_t slot, uint32_t type, const std::vector<game::Cargo>& inSlot, Clock::time_point now) {
    const auto found = g_requestedAdds.find({slot, type});
    if (found == g_requestedAdds.end()) return 0;
    const Requested& requested = found->second;
    const int arrived = std::clamp(seenCount(inSlot, type) - requested.baseline, 0, requested.count);
    const int left = requested.count - arrived;
    if (left == 0 || now - requested.since > kRequestBackstop) {
        g_requestedAdds.erase(found);
        return 0;
    }
    return left;
}

// The body's pieces by slot, for the log: "slot:kind" per piece.
std::string describeSlots(uint64_t ownerKey) {
    std::string text;
    for (const uint8_t slot : equip_sync::kMirroredSlots) {
        for (const game::Cargo& piece : game::slotPieces(ownerKey, slot)) {
            text += " " + std::to_string(slot) + ":" + std::to_string(piece.type);
        }
    }
    return text.empty() ? " none" : text;
}

// Deletes the body's pieces that a load dropped on the ground: they carry the mark the body's pieces get (game.h).
void removeOrphans() {
    for (const uint64_t handle : game::markedLooseCargo()) {
        game::removeCargoLater(handle);
        logger::write("equip_sync: removed a piece of the body's dropped by a load (%llx)", static_cast<unsigned long long>(handle));
    }
}

// A load drops the pieces its save held for the body at the start of gameplay; the pieces of a world being torn down look
// loose for a moment too, so the sweep runs once per gameplay, after the world has settled.
void removeOrphansOncePerGameplay() {
    static uint32_t sweptEpoch = 0;
    if (!sim_tick::gameplaySettled() || sweptEpoch == sim_tick::gameplayEpoch()) return;
    sweptEpoch = sim_tick::gameplayEpoch();
    removeOrphans();
}

// Brings the body's slots to what its partner carries: extra pieces deleted, missing kinds created. Requests the game
// has not served yet count as done.
void follow(uint64_t ownerKey, const std::vector<equip_sync::Held>& wanted, Clock::time_point now) {
    // Only a target that has not changed since the previous check is applied: a flapping partner would otherwise
    // create and delete pieces in the body's slots every few hundred milliseconds.
    const bool steady = wanted == g_lastWanted;
    g_lastWanted = wanted;
    if (!steady || now - g_lastChange < kSettle) return;
    if (ownerKey != g_protectedOwner) {
        // The pieces the body is born with stay: deleting one crashed the player entity's equipment code.
        g_protectedOwner = ownerKey;
        g_requestedAdds.clear();
        g_requestedDeletes.clear();
        g_protected.clear();
        for (const uint8_t slot : equip_sync::kMirroredSlots) {
            for (const game::Cargo& piece : game::slotPieces(ownerKey, slot)) g_protected.insert(piece.handle);
        }
    }
    if (heldBy(ownerKey) == wanted) {
        g_requestedAdds.clear();
        g_requestedDeletes.clear();
        if (g_awaitingMatch) logger::write("equip_sync: the body's slots now match the partner's:%s", describeSlots(ownerKey).c_str());
        g_awaitingMatch = false;
        return;
    }
    const std::string before = describeSlots(ownerKey);
    int changes = 0;
    std::string plan;
    std::set<uint64_t> present;
    for (const uint8_t slot : equip_sync::kMirroredSlots) {
        std::vector<equip_sync::Held> missing;
        for (const equip_sync::Held& want : wanted) {
            if (want.slot == slot) missing.push_back(want);
        }
        const std::vector<game::Cargo> inSlot = game::slotPieces(ownerKey, slot);
        for (const game::Cargo& piece : inSlot) {
            present.insert(piece.handle);
            if (g_requestedDeletes.contains(piece.handle)) continue;
            const auto match = std::find_if(missing.begin(), missing.end(),
                                            [&](const equip_sync::Held& m) { return m.type == piece.type; });
            if (match != missing.end()) {
                missing.erase(match);
            } else if (changes < kMaxChangesPerRound && !g_protected.contains(piece.handle) && !isGear(slot)) {
                game::removeCargoLater(piece.handle);
                g_requestedDeletes.insert(piece.handle);
                plan += " -" + std::to_string(slot) + ":" + std::to_string(piece.type);
                ++changes;
            }
        }
        std::map<uint32_t, int> needed;  // kind -> how many are missing in this slot
        // A worn-gear slot holds one piece and the body is born with some: never add to one that is not empty.
        if (!isGear(slot) || inSlot.empty()) {
            for (const equip_sync::Held& want : missing) ++needed[want.type];
        }
        for (const auto& [type, count] : needed) {
            const int toAsk = count - outstandingAdds(slot, type, inSlot, now);
            for (int i = 0; i < toAsk && changes < kMaxChangesPerRound; ++i) {
                if (!game::addSlotPiece(ownerKey, slot, type)) continue;
                Requested& requested = g_requestedAdds[{slot, type}];
                if (requested.count == 0) {
                    requested.baseline = seenCount(inSlot, type);
                    requested.since = now;
                }
                ++requested.count;
                plan += " +" + std::to_string(slot) + ":" + std::to_string(type);
                ++changes;
            }
        }
    }
    std::erase_if(g_requestedDeletes, [&](uint64_t handle) { return !present.contains(handle); });
    if (!changes) return;
    g_lastChange = now;
    g_awaitingMatch = true;
    logger::write("equip_sync: the body's slots now follow %zu carried pieces (%d changes:%s), before:%s", wanted.size(), changes,
                  plan.c_str(), before.c_str());
}

}  // namespace

namespace equip_sync {

void onFrame(const GameFrame& frame) {
    EquipHeader header;
    if (frame.type != kMsgEquipState || frame.payload.size() < sizeof(header)) return;
    std::memcpy(&header, frame.payload.data(), sizeof(header));
    if (header.count > kMaxHeld || frame.payload.size() != sizeof(header) + header.count * sizeof(Held)) return;
    std::vector<Held> held(header.count);
    if (header.count) std::memcpy(held.data(), frame.payload.data() + sizeof(header), header.count * sizeof(Held));
    std::sort(held.begin(), held.end(), lessHeld);
    g_peerHeld[frame.slot] = std::move(held);
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const auto now = Clock::now();
    if (!session.linked || now - g_lastCheck < kCheckInterval) return;
    g_lastCheck = now;
    const std::vector<equip_sync::Held> local = heldBy(0);
    report(net, local);
    const std::optional<uint64_t> body = remote_body::ownerKey();
    removeOrphansOncePerGameplay();
    if (!body) {
        g_protectedOwner = 0;  // the next body starts from a clean state
        g_lastWanted.clear();
        return;
    }
    if (local.empty()) return;  // the local player's own slots empty out as a load starts: the world is going away
    game::markOwnedCargo(*body);
    const auto peer = g_peerHeld.find(remote_body::slot());
    if (peer != g_peerHeld.end()) follow(*body, peer->second, now);
}

}  // namespace equip_sync
