// DEATH STRANDING 2: the partner's cargo where the game counts or moves cargo. The remote body owns a baggage owner
// holding a mirror of the partner's rack (rack_sync). Two hooks make that mirror count like carried cargo: the "carried
// by the player" set 0x1411d7530 (what the delivery and order checks count) also walks the remote's owner, scoped to
// that call so the manager's attached-owner list and Sam's movement code are untouched; and the slot functions (remove
// 0x141185ca0, add 0x141185b30) report a piece that moves between the remote's owner and a terminal or the local
// player's own owner, so the partner's machine can delete (a delivery, a take) or create (a give) its copy
// (docs/DS2_NOTES.md, "Partner cargo at a terminal").
#include "ds2/partner_cargo.h"

#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/orders_diag.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "remote_body.h"

namespace {

constexpr uintptr_t kCarriedSet = 0x1411d7530;  // (manager, collector) -> pieces collected
constexpr uintptr_t kWalkOwner = 0x141198ae0;   // (owner, collector, filter): the owner's pieces, child owners included
constexpr uintptr_t kCarriedFilter = 0x14A1D3730;
constexpr uintptr_t kSlotRemove = 0x141185ca0;  // (slot, piece): clears the piece's slot fields
constexpr uintptr_t kSlotAdd = 0x141185b30;     // (slot, piece): sets piece +0x98 (the slot's definition) and +0xA0 (the slot)
constexpr uintptr_t kHandOverGather = 0x14119b930;  // (query): fills the owner list the hand-over menu and delivery check use
constexpr uintptr_t kQueryOwners = 0x12008, kQueryCount = 0x12808;  // the query's owner pointer list and its int count
constexpr int32_t kQueryCapacity = 0x100;
constexpr uintptr_t kCollectorCount = 0x12000;
constexpr uintptr_t kSlotDefinition = 0x00, kDefinitionOwner = 0x08;
constexpr uintptr_t kOwnerCarrierType = 0x10, kOwnerParent = 0x40;
constexpr uintptr_t kPieceHandle = 0x18, kPieceOrderId = 0x28, kPieceSecondId = 0x30, kPieceItem = 0x38,
                    kPieceCategory = 0x82, kPieceDurability = 0x84, kPieceSlot = 0xA0;
constexpr uintptr_t kItemType = 0x44;
constexpr uint8_t kCarrierTerminal = 8, kCarrierTerminalShelf = 16;
constexpr uint64_t kLocalPlayerKey = 0;
constexpr int kMaxOwnerDepth = 8;
constexpr ULONGLONG kLeavingExpiryMs = 2000;
constexpr size_t kMaxQueued = 64;

using CarriedFn = uint32_t (*)(uintptr_t manager, uintptr_t collector);
using WalkFn = void (*)(uintptr_t owner, uintptr_t collector, const void* filter);
using SlotFn = void (*)(uintptr_t slot, uintptr_t piece);
using GatherFn = uintptr_t (*)(uintptr_t query);

GatherFn g_gather = nullptr;
CarriedFn g_carried = nullptr;
SlotFn g_remove = nullptr;
SlotFn g_add = nullptr;

// A piece taken out of a slot of the remote's owner or of the local player's: where it came from, until it is added again.
struct Leaving {
    uint64_t handle;  // the pool entry may be reused by a new piece
    uintptr_t originRoot;
    ULONGLONG at;
};

std::mutex g_mutex;
std::unordered_map<uintptr_t, Leaving> g_leaving;  // by piece address; guarded by g_mutex
game::PartnerMoves g_moves;                        // guarded by g_mutex

uintptr_t remoteOwner() {
    const auto key = remote_body::ownerKey();
    return key ? game::baggageOwner(*key) : 0;
}

uintptr_t rootOf(uintptr_t owner) {
    for (int depth = 0; owner && depth < kMaxOwnerDepth; ++depth) {
        const uintptr_t parent = decima::readPointer(owner + kOwnerParent);
        if (!parent) break;
        owner = parent;
    }
    return owner;
}

uintptr_t slotRoot(uintptr_t slot) {
    const uintptr_t definition = slot ? decima::readPointer(slot + kSlotDefinition) : 0;
    return definition ? rootOf(decima::readPointer(definition + kDefinitionOwner)) : 0;
}

bool isTerminal(uintptr_t owner) {
    uint8_t type = 0;
    return owner && decima::safeRead(owner + kOwnerCarrierType, type) &&
           (type == kCarrierTerminal || type == kCarrierTerminalShelf);
}

uint64_t handleOf(uintptr_t piece) {
    uint64_t handle = 0;
    decima::safeRead(piece + kPieceHandle, handle);
    return handle;
}

game::Cargo readPiece(uintptr_t piece) {
    game::Cargo cargo;
    uint64_t orderId = 0, secondId = 0;
    decima::safeRead(piece + kPieceOrderId, orderId);
    decima::safeRead(piece + kPieceSecondId, secondId);
    cargo.handle = handleOf(piece);
    cargo.orderId = game::isOrderId(orderId) ? orderId : 0;
    cargo.secondId = game::isOrderId(secondId) ? secondId : 0;
    decima::safeRead(decima::readPointer(piece + kPieceItem) + kItemType, cargo.type);
    decima::safeRead(piece + kPieceCategory, cargo.category);
    decima::safeRead(piece + kPieceDurability, cargo.durability);
    return cargo;
}

// Where a piece being added came from: the owner it was just removed from, or (an add with no remove) the slot it still points at.
uintptr_t originOf(uintptr_t piece) {
    std::optional<Leaving> left;
    {
        std::lock_guard lock(g_mutex);
        if (const auto found = g_leaving.find(piece); found != g_leaving.end()) {
            left = found->second;
            g_leaving.erase(found);
        }
    }
    if (!left) return slotRoot(decima::readPointer(piece + kPieceSlot));
    return left->handle == handleOf(piece) && GetTickCount64() - left->at <= kLeavingExpiryMs ? left->originRoot : 0;
}

void queue(std::vector<game::Cargo>& list, const game::Cargo& piece, const char* what) {
    logger::write("partner_cargo: %s (kind %u, order %llx)", what, piece.type, static_cast<unsigned long long>(piece.orderId));
    std::lock_guard lock(g_mutex);
    if (list.size() < kMaxQueued) list.push_back(piece);
}

// Reports a finished move of `piece` from the owner `origin` into the slot's owner `destination`.
void report(uintptr_t piece, uintptr_t origin, uintptr_t destination, uintptr_t remote, uintptr_t local) {
    if (origin == remote && isTerminal(destination)) {
        queue(g_moves.left, readPiece(piece), "the partner's piece went into a terminal");
    } else if (origin == remote && destination == local) {
        queue(g_moves.left, readPiece(piece), "the partner's piece was taken by the local player");
    } else if (origin == local && destination == remote) {
        queue(g_moves.given, readPiece(piece), "the local player's piece was given to the partner");
    }
}

uint32_t carriedDetour(uintptr_t manager, uintptr_t collector) {
    const uint32_t count = g_carried(manager, collector);
    const uintptr_t owner = remoteOwner();
    if (!owner) return count;
    reinterpret_cast<WalkFn>(ds2::at(kWalkOwner))(owner, collector, reinterpret_cast<const void*>(ds2::at(kCarriedFilter)));
    const uint32_t total = ds2::field<uint32_t>(collector, kCollectorCount);
    orders_diag::noteCarriedSet(_ReturnAddress(), total - count);
    return total;
}

bool holdsOrderPiece(uint64_t ownerKey) {
    const std::vector<game::Cargo> owned = game::ownedCargo(ownerKey);
    return std::any_of(owned.begin(), owned.end(), [](const game::Cargo& piece) { return piece.orderId != 0; });
}

// Adds the remote's owner to the query's list (past the area and active filters, which its position and its switched-off
// state would fail) when it carries order pieces, so the hand-over menu and the delivery check see them as carried.
bool appendRemoteOwner(uintptr_t query, uintptr_t owner, uint64_t ownerKey) {
    int32_t count = 0;
    if (!decima::safeRead(query + kQueryCount, count) || count < 0 || count >= kQueryCapacity) return false;
    for (int32_t i = 0; i < count; ++i) {
        if (decima::readPointer(query + kQueryOwners + i * sizeof(uintptr_t)) == owner) return false;
    }
    if (!holdsOrderPiece(ownerKey)) return false;
    ds2::field<uintptr_t>(query, kQueryOwners + count * sizeof(uintptr_t)) = owner;
    ds2::field<int32_t>(query, kQueryCount) = count + 1;
    return true;
}

uintptr_t gatherDetour(uintptr_t query) {
    const uintptr_t result = g_gather(query);
    const auto key = remote_body::ownerKey();
    const uintptr_t owner = key ? game::baggageOwner(*key) : 0;
    const bool appended = owner && appendRemoteOwner(query, owner, *key);
    orders_diag::noteHandOverGather(query, owner, appended);
    return result;
}

void removeDetour(uintptr_t slot, uintptr_t piece) {
    const uintptr_t remote = remoteOwner();
    const uintptr_t root = remote ? slotRoot(slot) : 0;
    if (root && (root == remote || root == game::baggageOwner(kLocalPlayerKey))) {
        const ULONGLONG now = GetTickCount64();
        std::lock_guard lock(g_mutex);
        std::erase_if(g_leaving, [now](const auto& entry) { return now - entry.second.at > kLeavingExpiryMs; });
        g_leaving[piece] = {handleOf(piece), root, now};
    }
    g_remove(slot, piece);
}

void addDetour(uintptr_t slot, uintptr_t piece) {
    const uintptr_t remote = remoteOwner();
    if (remote) {
        const uintptr_t origin = originOf(piece);
        const uintptr_t destination = slotRoot(slot);
        if (origin && destination != origin) report(piece, origin, destination, remote, game::baggageOwner(kLocalPlayerKey));
    }
    g_add(slot, piece);
}

}  // namespace

namespace partner_cargo {

void installEarly() {
    hooks::install("carried-by-player set", ds2::at(kCarriedSet), reinterpret_cast<void*>(&carriedDetour),
                   reinterpret_cast<void**>(&g_carried));
    hooks::install("hand-over gather", ds2::at(kHandOverGather), reinterpret_cast<void*>(&gatherDetour),
                   reinterpret_cast<void**>(&g_gather));
    hooks::install("baggage slot remove", ds2::at(kSlotRemove), reinterpret_cast<void*>(&removeDetour),
                   reinterpret_cast<void**>(&g_remove));
    hooks::install("baggage slot add", ds2::at(kSlotAdd), reinterpret_cast<void*>(&addDetour),
                   reinterpret_cast<void**>(&g_add));
}

}  // namespace partner_cargo

namespace game {

PartnerMoves takePartnerMoves() {
    std::lock_guard lock(g_mutex);
    return std::exchange(g_moves, {});
}

}  // namespace game
