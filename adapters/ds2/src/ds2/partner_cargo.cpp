// DEATH STRANDING 2: the partner's cargo at a terminal. The remote body owns a baggage owner holding a mirror of the
// partner's rack (rack_sync). Two hooks make that mirror count like carried cargo: the "carried by the player" set
// 0x1411d7530 (what the delivery and order checks count) also walks the remote's owner, scoped to that call so the
// manager's attached-owner list and Sam's movement code are untouched; and the slot functions (remove 0x141185ca0, add
// 0x141185b30) report a piece that moves from the remote's owner into a terminal, so the partner's machine can delete
// its copy (docs/DS2_NOTES.md, "Partner cargo at a terminal").
#include "ds2/partner_cargo.h"

#include <windows.h>

#include <mutex>
#include <unordered_map>
#include <vector>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "remote_body.h"

namespace {

constexpr uintptr_t kCarriedSet = 0x1411d7530;  // (manager, collector) -> pieces collected
constexpr uintptr_t kWalkOwner = 0x141198ae0;   // (owner, collector, filter): the owner's pieces, child owners included
constexpr uintptr_t kCarriedFilter = 0x14A1D3730;
constexpr uintptr_t kSlotRemove = 0x141185ca0;  // (slot, piece)
constexpr uintptr_t kSlotAdd = 0x141185b30;     // (slot, piece)
constexpr uintptr_t kCollectorCount = 0x12000;
constexpr uintptr_t kSlotDefinition = 0x00, kDefinitionOwner = 0x08;
constexpr uintptr_t kOwnerCarrierType = 0x10, kOwnerParent = 0x40;
constexpr uintptr_t kPieceOrderId = 0x28, kPieceItem = 0x38, kPieceSlot = 0xA0, kItemType = 0x44;
constexpr uint8_t kCarrierTerminal = 8, kCarrierTerminalShelf = 16;
constexpr int kMaxOwnerDepth = 8;
constexpr ULONGLONG kLeavingExpiryMs = 2000;
constexpr size_t kMaxQueued = 64;

using CarriedFn = uint32_t (*)(uintptr_t manager, uintptr_t collector);
using WalkFn = void (*)(uintptr_t owner, uintptr_t collector, const void* filter);
using SlotFn = void (*)(uintptr_t slot, uintptr_t piece);

CarriedFn g_carried = nullptr;
SlotFn g_remove = nullptr;
SlotFn g_add = nullptr;

std::mutex g_mutex;
std::unordered_map<uintptr_t, ULONGLONG> g_leaving;  // simulation thread: pieces taken out of the remote's owner
std::vector<game::Cargo> g_delivered;

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

uint32_t carriedDetour(uintptr_t manager, uintptr_t collector) {
    const uint32_t count = g_carried(manager, collector);
    const uintptr_t owner = remoteOwner();
    if (!owner) return count;
    reinterpret_cast<WalkFn>(ds2::at(kWalkOwner))(owner, collector, reinterpret_cast<const void*>(ds2::at(kCarriedFilter)));
    return ds2::field<uint32_t>(collector, kCollectorCount);
}

void removeDetour(uintptr_t slot, uintptr_t piece) {
    const uintptr_t owner = remoteOwner();
    if (owner && slotRoot(slot) == owner) {
        const ULONGLONG now = GetTickCount64();
        std::erase_if(g_leaving, [now](const auto& entry) { return now - entry.second > kLeavingExpiryMs; });
        g_leaving[piece] = now;
    }
    g_remove(slot, piece);
}

void addDetour(uintptr_t slot, uintptr_t piece) {
    const uintptr_t owner = remoteOwner();
    if (owner && isTerminal(slotRoot(slot))) {
        const bool left = g_leaving.erase(piece) > 0 || slotRoot(decima::readPointer(piece + kPieceSlot)) == owner;
        if (left) {
            game::Cargo delivered;
            decima::safeRead(piece + kPieceOrderId, delivered.orderId);
            decima::safeRead(decima::readPointer(piece + kPieceItem) + kItemType, delivered.type);
            logger::write("partner_cargo: the partner's piece (kind %u, order %llx) went into a terminal", delivered.type,
                          static_cast<unsigned long long>(delivered.orderId));
            std::lock_guard lock(g_mutex);
            if (g_delivered.size() < kMaxQueued) g_delivered.push_back(delivered);
        }
    }
    g_leaving.erase(piece);
    g_add(slot, piece);
}

}  // namespace

namespace partner_cargo {

void installEarly() {
    hooks::install("carried-by-player set", ds2::at(kCarriedSet), reinterpret_cast<void*>(&carriedDetour),
                   reinterpret_cast<void**>(&g_carried));
    hooks::install("baggage slot remove", ds2::at(kSlotRemove), reinterpret_cast<void*>(&removeDetour),
                   reinterpret_cast<void**>(&g_remove));
    hooks::install("baggage slot add", ds2::at(kSlotAdd), reinterpret_cast<void*>(&addDetour),
                   reinterpret_cast<void**>(&g_add));
}

}  // namespace partner_cargo

namespace game {

std::vector<Cargo> takeDeliveredByPartner() {
    std::lock_guard lock(g_mutex);
    std::vector<Cargo> out;
    out.swap(g_delivered);
    return out;
}

}  // namespace game
