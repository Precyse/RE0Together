// DEATH STRANDING 2: the local player's cargo through DSBaggageManager. Every piece of cargo (weapons and tools
// too) is a DSBaggage in the manager's pool; a carried one points at a slot of a baggage owner. The local player is
// the owner whose key is 0: its own slots hold the equipped gear (boots, skeleton, the backpack itself, weapons in
// hand), its child owner the backpack's contents, which is what is listed and moved. Adding and deleting go through the
// manager's own request queue (the script exports CreateAndAddBaggageToPlayer and DeleteBaggage), which takes the
// manager's lock and is served by the game on its next update, so any thread may ask. A piece on the ground is made
// the same way the game spawns world cargo: a create info (kind, world position, no owner) handed to the manager's
// create, which reserves the piece under the manager's lock and builds it on its update (docs/DS2_NOTES.md, "Cargo").
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
// +0x30 rotation, +0x60 owner, +0x68 slot kind.
constexpr size_t kCreateInfoSize = 0xF0;
constexpr uintptr_t kInfoKind = 0x10, kInfoPosition = 0x18;
constexpr double kPlaceLiftMetres = 0.5;  // a placed piece starts this far above its spot and falls onto the ground

// DSBaggageManager: the baggage pool and the baggage owners (players, vehicles, lockers, ...).
constexpr uintptr_t kPoolCount = 0x30, kPoolData = 0x38;
constexpr uintptr_t kOwnerCount = 0x24278, kOwnerData = 0x24280;
// A pool entry (DSBaggage).
constexpr size_t kBaggageSize = 0x160;
constexpr uintptr_t kBaggageHandle = 0x18;    // ~0 while the entry is free
constexpr uintptr_t kBaggageItem = 0x38;      // DSGameBaggageListItem: the cargo kind
constexpr uintptr_t kBaggagePosition = 0x40;  // world position, 3 doubles
constexpr uintptr_t kBaggageSlot = 0x98;      // the owner slot holding it, 0 when on the ground
constexpr uint64_t kFreeHandle = ~0ull;
// A baggage owner.
constexpr uintptr_t kOwnerKey = 0x18;  // 0 = the local player
constexpr uintptr_t kOwnerSlotCount = 0x28, kOwnerSlotData = 0x30;
constexpr uintptr_t kOwnerChildCount = 0x48, kOwnerChildData = 0x50;
constexpr size_t kSlotSize = 0x1D0;
constexpr uint64_t kLocalPlayerKey = 0;
// DSGameBaggageListItem: its LocalizedTextResource name and the kind id.
constexpr uintptr_t kItemName = 0x20, kItemType = 0x44;

constexpr bool kToBackpack = true;  // the create request's second argument: the backpack slot (kind 1)
constexpr int32_t kMaxPool = 1 << 16;
constexpr int32_t kMaxOwners = 1 << 14;
constexpr int32_t kMaxSlots = 256;
constexpr int kMaxOwnerDepth = 4;

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

std::vector<SlotRange> backpackSlots(uintptr_t manager) {
    std::vector<SlotRange> slots;
    const int32_t owners = readCount(manager + kOwnerCount, kMaxOwners);
    const uintptr_t data = decima::readPointer(manager + kOwnerData);
    for (int32_t i = 0; data && i < owners; ++i) {
        const uintptr_t owner = decima::readPointer(data + i * sizeof(uintptr_t));
        uint64_t key = kFreeHandle;
        if (owner && decima::safeRead(owner + kOwnerKey, key) && key == kLocalPlayerKey) {
            collectSlots(owner, false, 0, slots);
            break;
        }
    }
    return slots;
}

bool inSlots(const std::vector<SlotRange>& slots, uintptr_t slot) {
    for (const SlotRange& range : slots) {
        if (slot >= range.begin && slot < range.end) return true;
    }
    return false;
}

struct PoolEntry {
    uint64_t handle;
    uintptr_t item;
    uintptr_t slot;
    world_to_screen::Vec3 position;
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
        if (entry.handle != kFreeHandle && entry.item) out.push_back(entry);
    }
    return out;
}

std::string itemName(uintptr_t item) { return decima::localizedText(decima::readPointer(item + kItemName)); }

}  // namespace

namespace game {

std::vector<Cargo> carriedCargo() {
    std::vector<Cargo> out;
    const uintptr_t baggage = manager();
    const std::vector<SlotRange> slots = baggage ? backpackSlots(baggage) : std::vector<SlotRange>{};
    if (slots.empty()) return out;
    for (const PoolEntry& entry : livePool(baggage)) {
        uint32_t type = 0;
        if (inSlots(slots, entry.slot) && decima::safeRead(entry.item + kItemType, type)) {
            out.push_back({entry.handle, type, itemName(entry.item)});
        }
    }
    return out;
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
        out.push_back({entry.handle, type, entry.position});
    }
    return out;
}

bool addCargo(uint32_t type) {
    if (!code().createAndAdd || !manager()) return false;
    code().createAndAdd(type, kToBackpack);
    return true;
}

bool placeCargo(uint32_t type, const world_to_screen::Vec3& at) {
    const uintptr_t baggage = manager();
    if (!code().create || !baggage) return false;
    alignas(16) std::array<uint8_t, kCreateInfoSize> info{};
    code().initCreateInfo(info.data());
    std::memcpy(info.data() + kInfoKind, &type, sizeof(type));
    const double position[3] = {at.x, at.y, at.z + kPlaceLiftMetres};
    std::memcpy(info.data() + kInfoPosition, position, sizeof(position));
    uint64_t handle = kFreeHandle;
    code().create(baggage, &handle, info.data());
    return handle != kFreeHandle;
}

bool removeCargo(uint64_t handle) {
    if (!code().remove || !manager()) return false;
    code().remove(handle);
    return true;
}

}  // namespace game
