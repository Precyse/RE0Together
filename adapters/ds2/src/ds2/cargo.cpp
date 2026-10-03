// DEATH STRANDING 2: the local player's cargo through DSBaggageManager. Every piece of cargo (weapons and tools
// too) is a DSBaggage in the manager's pool; a carried one points at a slot of a baggage owner. The local player is
// the owner whose key is 0: its own slots hold the equipped gear (boots, skeleton, the backpack itself, weapons in
// hand), its child owner the backpack's contents, which is what is listed and moved. A vehicle is the owner whose key
// is its vehicle id; its bed is its slot of kind 0. Adding and deleting go through the
// manager's own request queue (the script exports CreateAndAddBaggageToPlayer and DeleteBaggage), which takes the
// manager's lock and is served by the game on its next update, so any thread may ask. A piece on the ground is made
// the same way the game spawns world cargo: a create info (kind, world position, no owner) handed to the manager's
// create, which reserves the piece under the manager's lock and builds it on its update (docs/DS2_NOTES.md, "Cargo").
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "decima/localized_text.h"
#include "decima/safe_read.h"
#include "game.h"
#include "log.h"
#include "pattern_scan.h"

namespace {

// DSBaggageManager::CreateAndAddBaggageToPlayer(u32 type, bool backpack): `mov rdx, [DSBaggageManager]` at +15.
constexpr const char* kCreateAndAdd =
    "48 89 5C 24 08 57 48 83 EC 30 0F B6 FA 8B D9 48 8B 15 ?? ?? ?? ?? 48 8D 4C 24 20 48 81 C2 38 67 03 00";
constexpr int kManagerDisp = 18, kManagerEnd = 22;
// DSBaggageManager::DeleteBaggage(u64 handle).
constexpr const char* kDelete =
    "40 53 48 83 EC 30 48 8B 15 ?? ?? ?? ?? 48 8B D9 48 81 C2 38 67 03 00 48 8D 4C 24 20 E8";
// DSBaggageManager::Create(manager, u64* handleOut, const CreateInfo*): 0x1411c5c00 in build 23923251.
constexpr const char* kCreate = "40 56 41 56 41 57 48 83 EC 40 48 83 79 10 00 4D 8B F8 4C 8B F2 48 8B F1";
// The create info's constructor (defaults: no kind, no owner, identity rotation): 0x140abd190.
constexpr const char* kInitCreateInfo =
    "48 83 EC 28 33 D2 48 89 11 48 89 51 08 C7 41 10 FF FF FF FF 89 51 14 C5 F9 57 C0 C5 F8 11 41 18";
// The create info (0xF0 bytes; the type-2 request builds one on its stack): +0x10 kind, +0x18 world position,
// +0x30 rotation, +0x60 owner (none: on the ground), +0x68 slot kind.
constexpr size_t kCreateInfoSize = 0xF0;
constexpr uintptr_t kInfoOrderId = 0x00, kInfoSecondId = 0x08, kInfoKind = 0x10, kInfoPosition = 0x18, kInfoOwner = 0x60,
                    kInfoSlotKind = 0x68, kInfoCategory = 0x69, kInfoDurability = 0xB4;
constexpr double kPlaceLiftMetres = 0.5;  // a placed piece starts this far above its spot and falls onto the ground

// DSBaggageManager: the baggage pool and the baggage owners (players, vehicles, lockers, ...).
constexpr uintptr_t kPoolCount = 0x30, kPoolData = 0x38;
constexpr uintptr_t kOwnerCount = 0x24278, kOwnerData = 0x24280;
// A pool entry (DSBaggage).
constexpr size_t kBaggageSize = 0x160;
constexpr uintptr_t kBaggageHandle = 0x18;    // ~0 while the entry is free
constexpr uintptr_t kBaggageItem = 0x38;      // DSGameBaggageListItem: the cargo kind
constexpr uintptr_t kBaggagePosition = 0x40;  // world position, 3 doubles
constexpr uintptr_t kBaggageOrderId = 0x28;   // order id (piece index in bits 52..60), 0 for plain cargo
constexpr uintptr_t kBaggageSecondId = 0x30;  // second mission id of supply and collect style pieces
constexpr uintptr_t kBaggageCategory = 0x82;  // mission cargo category byte
constexpr uintptr_t kBaggageDurability = 0x84;
constexpr uintptr_t kBaggageSlot = 0x98;      // the owner slot holding it, 0 when on the ground
constexpr uint64_t kFreeHandle = ~0ull;
// A baggage owner.
constexpr uintptr_t kOwnerKey = 0x18;  // 0 = the local player
constexpr uintptr_t kOwnerSlotCount = 0x28, kOwnerSlotData = 0x30;
constexpr uintptr_t kOwnerChildCount = 0x48, kOwnerChildData = 0x50;
constexpr size_t kSlotSize = 0x1D0;
constexpr uint64_t kLocalPlayerKey = 0;
// An order id: a mission number in the low bits and an order type in bits 32..37; anything else is plain cargo.
constexpr uint64_t kOrderNumberMask = 0x1FFFFFFF;
constexpr uint64_t kOrderTypeMask = 0x3Full << 32;
constexpr uint8_t kBackpackSlotKind = 1;  // the main load of the backpack owner
constexpr uint8_t kBedSlotKind = 0;  // a vehicle's cargo bed (its other slots, kinds 35-37, are not mirrored)
constexpr uint8_t kNoSlotKind = 0xFF;
// DSGameBaggageListItem: its LocalizedTextResource name and the kind id.
constexpr uintptr_t kItemName = 0x20, kItemType = 0x44;

constexpr bool kToBackpack = true;  // the create request's second argument: the backpack slot (kind 1)
constexpr int32_t kMaxPool = 1 << 16;
constexpr int32_t kMaxOwners = 1 << 14;
constexpr int32_t kMaxSlots = 256;
constexpr int kMaxOwnerDepth = 4;
constexpr size_t kMaxOwnerTree = 64;

using CreateAndAddFn = void (*)(uint32_t type, bool backpack);
using DeleteFn = void (*)(uint64_t handle);
using CreateFn = uint64_t* (*)(uintptr_t manager, uint64_t* handle, const void* info);
using InitCreateInfoFn = void* (*)(void* info);

struct Code {
    uintptr_t managerGlobal = 0;
    CreateAndAddFn createAndAdd = nullptr;
    DeleteFn remove = nullptr;
    CreateFn create = nullptr;
    InitCreateInfoFn initCreateInfo = nullptr;
};

Code findCode() {
    const uintptr_t createAndAdd = pattern_scan::find(kCreateAndAdd);
    const uintptr_t remove = pattern_scan::find(kDelete);
    const uintptr_t create = pattern_scan::find(kCreate);
    const uintptr_t initInfo = pattern_scan::find(kInitCreateInfo);
    if (!createAndAdd || !remove || !create || !initInfo) {
        logger::write("cargo: baggage calls not found (add %d, delete %d, create %d, info %d)", createAndAdd != 0,
                      remove != 0, create != 0, initInfo != 0);
        return {};
    }
    const Code code{pattern_scan::ripTarget(createAndAdd, kManagerDisp, kManagerEnd),
                    reinterpret_cast<CreateAndAddFn>(createAndAdd), reinterpret_cast<DeleteFn>(remove),
                    reinterpret_cast<CreateFn>(create), reinterpret_cast<InitCreateInfoFn>(initInfo)};
    logger::write("cargo: baggage manager %p", reinterpret_cast<void*>(code.managerGlobal));
    return code;
}

const Code& code() {
    static const Code found = findCode();
    return found;
}

uintptr_t manager() { return code().managerGlobal ? decima::readPointer(code().managerGlobal) : 0; }

int32_t readCount(uintptr_t address, int32_t limit) {
    int32_t count = 0;
    return decima::safeRead(address, count) && count > 0 && count <= limit ? count : 0;
}

struct SlotRange {
    uintptr_t begin, end;
};

// The slot arrays of an owner's children and of theirs (with `withOwn`, the owner's own as well).
void collectSlots(uintptr_t owner, bool withOwn, int depth, std::vector<SlotRange>& out) {
    const int32_t slots = readCount(owner + kOwnerSlotCount, kMaxSlots);
    if (const uintptr_t data = decima::readPointer(owner + kOwnerSlotData); withOwn && data && slots) {
        out.push_back({data, data + slots * kSlotSize});
    }
    if (depth >= kMaxOwnerDepth) return;
    const int32_t children = readCount(owner + kOwnerChildCount, kMaxOwners);
    const uintptr_t childData = decima::readPointer(owner + kOwnerChildData);
    for (int32_t i = 0; childData && i < children; ++i) {
        if (const uintptr_t child = decima::readPointer(childData + i * sizeof(uintptr_t))) {
            collectSlots(child, true, depth + 1, out);
        }
    }
}

uintptr_t findOwner(uintptr_t manager, uint64_t wanted) {
    const int32_t owners = readCount(manager + kOwnerCount, kMaxOwners);
    const uintptr_t data = decima::readPointer(manager + kOwnerData);
    for (int32_t i = 0; data && i < owners; ++i) {
        const uintptr_t owner = decima::readPointer(data + i * sizeof(uintptr_t));
        uint64_t key = kFreeHandle;
        if (owner && decima::safeRead(owner + kOwnerKey, key) && key == wanted) return owner;
    }
    return 0;
}

// Whether `owner` (a baggage owner other than the local player's) is the local player's own owner or sits in its tree
// of child owners (the backpack). The remote body's owner shares the local player's backpack objects, so writing to
// anything reachable from the local player's tree would change the local inventory: every write to a partner's
// owner is refused when this holds.
bool sharesWithLocalPlayer(uintptr_t manager, uint64_t ownerKey, uintptr_t owner) {
    if (ownerKey == kLocalPlayerKey) return false;  // the caller is the local player's own code path
    std::vector<uintptr_t> tree;
    const uintptr_t player = findOwner(manager, kLocalPlayerKey);
    if (!player) return false;
    tree.push_back(player);
    for (size_t next = 0; next < tree.size() && tree.size() < kMaxOwnerTree; ++next) {
        const int32_t children = readCount(tree[next] + kOwnerChildCount, kMaxOwners);
        const uintptr_t childData = decima::readPointer(tree[next] + kOwnerChildData);
        for (int32_t i = 0; childData && i < children; ++i) {
            if (const uintptr_t child = decima::readPointer(childData + i * sizeof(uintptr_t))) tree.push_back(child);
        }
    }
    const bool shared = std::find(tree.begin(), tree.end(), owner) != tree.end();
    if (shared) logger::write("cargo: refused a write to baggage owner %llx: it is part of the local player's tree",
                              static_cast<unsigned long long>(ownerKey));
    return shared;
}

std::vector<SlotRange> backpackSlots(uintptr_t manager) {
    std::vector<SlotRange> slots;
    if (const uintptr_t player = findOwner(manager, kLocalPlayerKey)) collectSlots(player, false, 0, slots);
    return slots;
}

// The owner's own slots of one kind.
std::vector<SlotRange> slotsOfKind(uintptr_t owner, uint8_t kind) {
    std::vector<SlotRange> out;
    const int32_t slots = readCount(owner + kOwnerSlotCount, kMaxSlots);
    const uintptr_t data = decima::readPointer(owner + kOwnerSlotData);
    for (int32_t i = 0; data && i < slots; ++i) {
        const uintptr_t slot = data + i * kSlotSize;
        uint8_t slotKind = kNoSlotKind;
        if (decima::safeRead(slot, slotKind) && slotKind == kind) out.push_back({slot, slot + kSlotSize});
    }
    return out;
}

bool inSlots(const std::vector<SlotRange>& slots, uintptr_t slot) {
    for (const SlotRange& range : slots) {
        if (slot >= range.begin && slot < range.end) return true;
    }
    return false;
}

bool isOrderId(uint64_t id);

struct PoolEntry {
    uint64_t handle;
    uintptr_t item;
    uintptr_t slot;
    world_to_screen::Vec3 position;
    uint64_t orderId, secondId;
    uint8_t category;
    float durability;
};

// Every piece in the pool, from one copy of it (the game may change entries while we read).
std::vector<PoolEntry> livePool(uintptr_t manager) {
    std::vector<PoolEntry> out;
    const int32_t count = readCount(manager + kPoolCount, kMaxPool);
    const uintptr_t pool = decima::readPointer(manager + kPoolData);
    std::vector<uint8_t> entries(count * kBaggageSize);
    if (!pool || !count || !decima::safeCopy(entries.data(), pool, entries.size())) return out;
    for (int32_t i = 0; i < count; ++i) {
        const uint8_t* raw = entries.data() + i * kBaggageSize;
        PoolEntry entry;
        std::memcpy(&entry.handle, raw + kBaggageHandle, sizeof(entry.handle));
        std::memcpy(&entry.item, raw + kBaggageItem, sizeof(entry.item));
        std::memcpy(&entry.slot, raw + kBaggageSlot, sizeof(entry.slot));
        std::memcpy(&entry.position, raw + kBaggagePosition, sizeof(entry.position));
        std::memcpy(&entry.orderId, raw + kBaggageOrderId, sizeof(entry.orderId));
        std::memcpy(&entry.secondId, raw + kBaggageSecondId, sizeof(entry.secondId));
        std::memcpy(&entry.category, raw + kBaggageCategory, sizeof(entry.category));
        std::memcpy(&entry.durability, raw + kBaggageDurability, sizeof(entry.durability));
        if (entry.handle != kFreeHandle && entry.item) out.push_back(entry);
    }
    return out;
}

std::string itemName(uintptr_t item) { return decima::localizedText(decima::readPointer(item + kItemName)); }

std::vector<game::Cargo> piecesIn(uintptr_t manager, const std::vector<SlotRange>& slots) {
    std::vector<game::Cargo> out;
    if (slots.empty()) return out;
    for (const PoolEntry& entry : livePool(manager)) {
        uint32_t type = 0;
        if (inSlots(slots, entry.slot) && decima::safeRead(entry.item + kItemType, type)) {
            out.push_back({entry.handle, type, itemName(entry.item), isOrderId(entry.orderId) ? entry.orderId : 0,
                           entry.secondId, entry.category, entry.durability});
        }
    }
    return out;
}

// The manager's own create, as world cargo is spawned: with an owner the piece goes into that owner's slot of
// `slotKind`, without one it lies at `at`. `order` (optional) carries the order link of a mission piece.
bool createPiece(uintptr_t manager, uint32_t type, const world_to_screen::Vec3& at, uintptr_t owner, uint8_t slotKind,
                 const game::Cargo* order = nullptr) {
    if (!code().create) return false;
    alignas(16) std::array<uint8_t, kCreateInfoSize> info{};
    code().initCreateInfo(info.data());
    std::memcpy(info.data() + kInfoKind, &type, sizeof(type));
    const double position[3] = {at.x, at.y, at.z};
    std::memcpy(info.data() + kInfoPosition, position, sizeof(position));
    if (owner) {
        std::memcpy(info.data() + kInfoOwner, &owner, sizeof(owner));
        info[kInfoSlotKind] = slotKind;
    }
    if (order) {
        std::memcpy(info.data() + kInfoOrderId, &order->orderId, sizeof(order->orderId));
        std::memcpy(info.data() + kInfoSecondId, &order->secondId, sizeof(order->secondId));
        info[kInfoCategory] = order->category;
        if (order->durability > 0) std::memcpy(info.data() + kInfoDurability, &order->durability, sizeof(order->durability));
    }
    uint64_t handle = kFreeHandle;
    code().create(manager, &handle, info.data());
    return handle != kFreeHandle;
}

bool isOrderId(uint64_t id) { return (id & kOrderNumberMask) != 0 && (id & kOrderTypeMask) != 0; }

// Where the pieces with this order id (and piece index) are in this world. Deleting an order piece deletes every
// piece sharing its id, so a second one with the same id must never be created.
struct OrderPieces {
    bool carried = false;             // one is in the local backpack
    std::vector<uint64_t> elsewhere;  // handles of the others (a locker, a shelf, the ground)
};

OrderPieces findOrderPieces(uintptr_t manager, uint64_t orderId) {
    OrderPieces found;
    const std::vector<SlotRange> backpack = backpackSlots(manager);
    for (const PoolEntry& entry : livePool(manager)) {
        if (entry.orderId != orderId) continue;
        if (inSlots(backpack, entry.slot)) {
            found.carried = true;
        } else {
            found.elsewhere.push_back(entry.handle);
        }
    }
    return found;
}

// The local player's backpack owner: its child owner that has a slot of the main-load kind.
uintptr_t backpackOwner(uintptr_t manager) {
    const uintptr_t player = findOwner(manager, kLocalPlayerKey);
    const int32_t children = player ? readCount(player + kOwnerChildCount, kMaxOwners) : 0;
    const uintptr_t childData = player ? decima::readPointer(player + kOwnerChildData) : 0;
    for (int32_t i = 0; childData && i < children; ++i) {
        const uintptr_t child = decima::readPointer(childData + i * sizeof(uintptr_t));
        if (child && !slotsOfKind(child, kBackpackSlotKind).empty()) return child;
    }
    return 0;
}

}  // namespace

namespace game {

std::vector<Cargo> carriedCargo() {
    const uintptr_t baggage = manager();
    return baggage ? piecesIn(baggage, backpackSlots(baggage)) : std::vector<Cargo>{};
}

std::vector<Cargo> slotPieces(uint64_t ownerKey, uint8_t slotKind) {
    const uintptr_t baggage = manager();
    const uintptr_t owner = baggage ? findOwner(baggage, ownerKey) : 0;
    return owner && !sharesWithLocalPlayer(baggage, ownerKey, owner) ? piecesIn(baggage, slotsOfKind(owner, slotKind))
                                                                     : std::vector<Cargo>{};
}

bool addSlotPiece(uint64_t ownerKey, uint8_t slotKind, uint32_t type) {
    const uintptr_t baggage = manager();
    const uintptr_t owner = baggage ? findOwner(baggage, ownerKey) : 0;
    return owner && !sharesWithLocalPlayer(baggage, ownerKey, owner) && createPiece(baggage, type, {}, owner, slotKind);
}

std::vector<Cargo> vehicleCargo(uint64_t vehicle) { return slotPieces(vehicle, kBedSlotKind); }

bool addVehicleCargo(uint64_t vehicle, uint32_t type) { return addSlotPiece(vehicle, kBedSlotKind, type); }

std::optional<uint64_t> findOrderPiece(uint64_t orderId) {
    const uintptr_t baggage = manager();
    if (!baggage || !isOrderId(orderId)) return std::nullopt;
    const OrderPieces found = findOrderPieces(baggage, orderId);
    if (found.elsewhere.empty()) return std::nullopt;
    return found.elsewhere.front();
}

std::vector<LooseCargo> looseCargo(const world_to_screen::Vec3& around, double radius) {
    std::vector<LooseCargo> out;
    const uintptr_t baggage = manager();
    if (!baggage) return out;
    for (const PoolEntry& entry : livePool(baggage)) {
        uint32_t type = 0;
        const world_to_screen::Vec3 offset = entry.position - around;
        if (entry.slot || dot(offset, offset) > radius * radius || !decima::safeRead(entry.item + kItemType, type)) {
            continue;
        }
        out.push_back({entry.handle, type, entry.position, isOrderId(entry.orderId) ? entry.orderId : 0});
    }
    return out;
}

AddResult addCargo(const Cargo& piece) {
    const uintptr_t baggage = manager();
    if (!baggage) return AddResult::Retry;
    if (isOrderId(piece.orderId) || isOrderId(piece.secondId)) {
        if (isOrderId(piece.orderId)) {
            const OrderPieces existing = findOrderPieces(baggage, piece.orderId);
            if (existing.carried) {
                logger::write("cargo: refused to create order piece %llx: the backpack already holds it",
                              static_cast<unsigned long long>(piece.orderId));
                return AddResult::Refused;
            }
            if (!existing.elsewhere.empty()) {
                // This world still holds the piece the partner gave away (both worlds start from one save): remove
                // that stale copy, then create the real one once the game has served the deletion.
                for (const uint64_t handle : existing.elsewhere) removeCargo(handle);
                logger::write("cargo: removed %zu stale copies of order piece %llx before receiving it",
                              existing.elsewhere.size(), static_cast<unsigned long long>(piece.orderId));
                return AddResult::Retry;
            }
        }
        const uintptr_t owner = backpackOwner(baggage);
        return owner && createPiece(baggage, piece.type, {}, owner, kBackpackSlotKind, &piece) ? AddResult::Done
                                                                                            : AddResult::Refused;
    }
    if (piece.durability > 0) {
        // A damaged or partly used piece keeps its durability: it is created with the manager's own create, which takes
        // it, instead of the request queue's create (a fresh piece).
        const uintptr_t owner = backpackOwner(baggage);
        return owner && createPiece(baggage, piece.type, {}, owner, kBackpackSlotKind, &piece) ? AddResult::Done
                                                                                            : AddResult::Refused;
    }
    if (!code().createAndAdd) return AddResult::Refused;
    code().createAndAdd(piece.type, kToBackpack);
    return AddResult::Done;
}

bool placeCargo(uint32_t type, const world_to_screen::Vec3& at) {
    const uintptr_t baggage = manager();
    return baggage && createPiece(baggage, type, {at.x, at.y, at.z + kPlaceLiftMetres}, 0, kNoSlotKind);
}

bool removeCargo(uint64_t handle) {
    if (!code().remove || !manager()) return false;
    code().remove(handle);
    return true;
}

}  // namespace game
