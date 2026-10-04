#include "equip_sync.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "game.h"
#include "log.h"
#include "remote_body.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kCheckInterval = std::chrono::milliseconds(500);
constexpr int kMaxChangesPerRound = 6;  // a burst (a cargo theft on the partner) is spread over several rounds
constexpr auto kSettle = std::chrono::seconds(2);  // the game serves create and delete requests on its next update

// Net thread only.
Clock::time_point g_lastCheck;
Clock::time_point g_lastChange;
std::vector<equip_sync::Held> g_reported;
std::vector<equip_sync::Held> g_lastWanted;
uint64_t g_protectedOwner = 0;
std::set<uint64_t> g_protected;  // handles of the pieces the body was born with
std::map<uint8_t, std::vector<equip_sync::Held>> g_peerHeld;  // by source slot

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

void report(NetClient& net) {
    const std::vector<equip_sync::Held> held = heldBy(0);
    if (held == g_reported) return;
    const equip_sync::EquipHeader header{static_cast<uint32_t>(held.size()), 0};
    std::vector<uint8_t> payload(sizeof(header) + held.size() * sizeof(equip_sync::Held));
    std::memcpy(payload.data(), &header, sizeof(header));
    if (!held.empty()) std::memcpy(payload.data() + sizeof(header), held.data(), held.size() * sizeof(equip_sync::Held));
    if (!net.send(equip_sync::kMsgEquipState, true, proto::kSlotAll, payload)) return;
    g_reported = held;
    logger::write("equip_sync: reported %zu carried pieces", held.size());
}

// Brings the body's slots to what its partner carries: extra pieces deleted, missing kinds created.
void follow(uint64_t ownerKey, const std::vector<equip_sync::Held>& wanted, Clock::time_point now) {
    // Only a target that has not changed since the previous check is applied: a flapping partner would otherwise
    // create and delete pieces in the body's slots every few hundred milliseconds.
    const bool steady = wanted == g_lastWanted;
    g_lastWanted = wanted;
    if (!steady || now - g_lastChange < kSettle) return;
    if (ownerKey != g_protectedOwner) {
        // The pieces the body is born with (its shoes, skeleton and so on): deleting one crashed the player entity's
        // equipment code, so they are never deleted, only added to.
        g_protectedOwner = ownerKey;
        g_protected.clear();
        for (const uint8_t slot : equip_sync::kMirroredSlots) {
            for (const game::Cargo& piece : game::slotPieces(ownerKey, slot)) g_protected.insert(piece.handle);
        }
    }
    std::vector<equip_sync::Held> have = heldBy(ownerKey);
    if (have == wanted) return;
    g_lastChange = now;
    int changes = 0;
    for (const uint8_t slot : equip_sync::kMirroredSlots) {
        std::vector<equip_sync::Held> missing;
        for (const equip_sync::Held& want : wanted) {
            if (want.slot == slot) missing.push_back(want);
        }
        for (const game::Cargo& piece : game::slotPieces(ownerKey, slot)) {
            const auto match = std::find_if(missing.begin(), missing.end(),
                                            [&](const equip_sync::Held& m) { return m.type == piece.type; });
            if (match != missing.end()) {
                missing.erase(match);
            } else if (changes < kMaxChangesPerRound && !g_protected.contains(piece.handle)) {
                game::removeCargoLater(piece.handle);
                ++changes;
            }
        }
        for (const equip_sync::Held& want : missing) {
            if (changes >= kMaxChangesPerRound) break;
            changes += game::addSlotPiece(ownerKey, slot, want.type);
        }
    }
    if (changes) logger::write("equip_sync: the body's slots now follow %zu carried pieces (%d changes)", wanted.size(), changes);
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
    report(net);
    const std::optional<uint64_t> body = remote_body::ownerKey();
    const auto peer = g_peerHeld.find(remote_body::slot());
    if (body && peer != g_peerHeld.end()) follow(*body, peer->second, now);
}

}  // namespace equip_sync
