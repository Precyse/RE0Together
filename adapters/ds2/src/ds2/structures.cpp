// DEATH STRANDING 2: structures the player places (ladders so far) and removes. The host reports each one placed by
// its player and each removal; a guest rebuilds them with the game's own player-build recipe (factory, Init with the
// host's id, submit) and removes them through the object's RequestRemove, on the simulation thread just ahead of the
// engine's object update. The guest's own placements are refused, so the host's world stays the one truth
// (docs/DS2_NOTES.md, "Structure sync").
#include "ds2/structures.h"

#include <intrin.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <mutex>
#include <vector>

#include "decima/safe_read.h"
#include "decima/world_transform.h"
#include "ds2/engine.h"
#include "ds2/sim_tick.h"
#include "game.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kConstructionManagerGlobal = 0x14623EAD8;
constexpr uintptr_t kFactory = 0x14125b570;      // (manager, kind) -> descriptor
constexpr uintptr_t kInit = 0x1412d8f50;         // (descriptor, const WorldTransform*, u8 origin, u32 id)
constexpr uintptr_t kSubmit = 0x141263280;       // (manager, descriptor)
constexpr uintptr_t kObjectById = 0x141269580;   // (manager, u32 id) -> DSConstructionObject or 0
constexpr uintptr_t kRemoveByPlayer = 0x1412d9c80;  // ConstructionExportedFunctions::RemoveConstructionByPlayer(u32 id)
constexpr uintptr_t kRequestRemove = 0x14127f5f0;        // DSConstructionObject slot 4 (object, factor, bool, bool)
constexpr uintptr_t kLadderRequestRemove = 0x1412f3bf0;     // the overrides of the base RequestRemove:
constexpr uintptr_t kCatapultRequestRemove = 0x141312770;  // the ladder's, the catapult's
constexpr uintptr_t kSafetyHouseRequestRemove = 0x141308470;  // and the safety house's
constexpr uintptr_t kPlayerPlacementCaller = 0x14200cc0d;  // the submit inside the held-item placement (slot 46)

// DSConstructionCreationDescriptor.
constexpr uintptr_t kDescKind = 0x10, kDescSubKind = 0x60, kDescId = 0x6C, kDescLevel = 0x70, kDescGuid = 0x74,
                    kDescDurability = 0x84, kDescFlags = 0x94, kDescOwner = 0xE8;
constexpr uintptr_t kDescTransform = 0x18;
constexpr uint8_t kOriginPlayer = 1;
constexpr uint16_t kPlayerBuildFlags = 0x101;
constexpr uint32_t kPlayerOwner = 1;  // descriptor +0xE8 as the player's own deploy leaves it
constexpr uintptr_t kObjectId = 0x70;  // DSConstructionObject: its construction id
constexpr uint32_t kMaxRemoveFactor = 0x40;  // the entry is also reached with a pointer-sized second argument
constexpr size_t kMaxQueued = 256;

using Fn8 = uint64_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using FactoryFn = uintptr_t (*)(uintptr_t manager, uint8_t kind);
using InitFn = void (*)(uintptr_t desc, const void* transform, uint8_t origin, uint32_t id);
using SubmitFn = void (*)(uintptr_t manager, uintptr_t desc);
using ObjectByIdFn = uintptr_t (*)(uintptr_t manager, uint32_t id);
using RemoveByPlayerFn = void (*)(uint32_t id);

Fn8 g_submit = nullptr, g_requestRemove = nullptr;
std::atomic<bool> g_host{false};
std::atomic<bool> g_guest{false};
thread_local bool t_applying = false;  // the adapter itself is building or removing: never reported or refused

std::mutex g_mutex;
std::vector<struct_wire::Placed> g_placed;     // host: to send
std::vector<struct_wire::Remove> g_removed;    // host: to send
std::vector<struct_wire::Placed> g_toCreate;   // guest: to build on the simulation thread
std::vector<struct_wire::Remove> g_toRemove;   // guest: to remove on the simulation thread

uintptr_t fileVa(void* address) {
    static const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    return reinterpret_cast<uintptr_t>(address) - base + 0x140000000;
}

uintptr_t manager() { return decima::readPointer(ds2::at(kConstructionManagerGlobal)); }

// Whether a qword of a descriptor's own fields looks like a pointer (a user-space address): those are never sent.
bool looksLikePointer(uint64_t value) {
    constexpr uint64_t kLowest = 0x10000;
    constexpr uint64_t kUserSpaceEnd = 0x800000000000;
    return value >= kLowest && value < kUserSpaceEnd && (value & 7) == 0;
}

bool describe(uintptr_t desc, struct_wire::Placed& out) {
    struct_wire::Create& c = out.create;
    c = {};
    decima::safeRead(desc + kDescKind, c.kind);
    const struct_wire::KindInfo* info = struct_wire::kindInfo(c.kind);
    if (!info) return false;
    c.tailBytes = info->tailBytes;
    decima::safeRead(desc + kDescSubKind, c.subKind);
    decima::safeRead(desc + kDescLevel, c.level);
    decima::safeRead(desc + kDescId, c.id);
    decima::safeRead(desc + kDescDurability, c.durability);
    out.tail.assign(c.tailBytes, 0);
    if (!decima::safeCopy(c.guid, desc + kDescGuid, sizeof(c.guid)) ||
        !decima::safeCopy(c.transform, desc + kDescTransform, sizeof(c.transform)) ||
        !decima::safeCopy(out.tail.data(), desc + struct_wire::kBaseDescriptorBytes, out.tail.size())) {
        return false;
    }
    for (const uint8_t skip : {info->skipFirst, info->skipSecond}) {
        if (skip != struct_wire::kNoSkip) std::memset(out.tail.data() + skip, 0, sizeof(uint64_t));
    }
    for (size_t offset = 0; offset + sizeof(uint64_t) <= out.tail.size(); offset += sizeof(uint64_t)) {
        uint64_t qword;
        std::memcpy(&qword, out.tail.data() + offset, sizeof(qword));
        if (looksLikePointer(qword)) {
            logger::write("structures: kind %u tail qword +0x%zx looks like a pointer (%llx), not sent", c.kind, offset,
                          static_cast<unsigned long long>(qword));
            std::memset(out.tail.data() + offset, 0, sizeof(qword));
        }
    }
    return true;
}

// A structure placed by the player's held item (not loaded from the save, not built by an online handler or a mission).
uint64_t submitDetour(uintptr_t mgr, uintptr_t desc, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f, uintptr_t g,
                      uintptr_t h) {
    if (!t_applying && fileVa(_ReturnAddress()) == kPlayerPlacementCaller) {
        if (g_guest.load()) {
            logger::write("structures: a placement by the guest's own player was refused");
            return 0;
        }
        struct_wire::Placed placed;
        if (g_host.load() && describe(desc, placed)) {
            std::string tail;
            for (const uint8_t byte : placed.tail) {
                char hex[4];
                snprintf(hex, sizeof(hex), "%02x ", byte);
                tail += hex;
            }
            logger::write("structures: placed kind %u sub %u level %u id %u, tail %s", placed.create.kind, placed.create.subKind,
                          placed.create.level, placed.create.id, tail.c_str());
            std::lock_guard lock(g_mutex);
            if (g_placed.size() < kMaxQueued) g_placed.push_back(std::move(placed));
        }
    }
    return g_submit(mgr, desc, c, d, e, f, g, h);
}

uintptr_t objectById(uintptr_t mgr, uint32_t id);

void noteRemoval(uintptr_t object, uintptr_t factor) {
    if (t_applying || !g_host.load() || factor > kMaxRemoveFactor) return;
    uint32_t id = 0;
    if (!decima::safeRead(object + kObjectId, id) || objectById(manager(), id) != object) return;
    std::lock_guard lock(g_mutex);
    const bool queued = std::any_of(g_removed.begin(), g_removed.end(), [&](const auto& r) { return r.id == id; });
    if (queued || g_removed.size() >= kMaxQueued) return;
    logger::write("structures: removed id %u factor %llu", id, static_cast<unsigned long long>(factor));
    g_removed.push_back({id, static_cast<uint8_t>(factor), {}});
}

uint64_t requestRemoveDetour(uintptr_t object, uintptr_t factor, uintptr_t b, uintptr_t c, uintptr_t e, uintptr_t f,
                             uintptr_t g, uintptr_t h) {
    noteRemoval(object, factor);
    return g_requestRemove(object, factor, b, c, e, f, g, h);
}

// One detour and trampoline per override of RequestRemove (the ladder's, the catapult's, the safety house's).
template <int N>
struct Override {
    static inline Fn8 original = nullptr;
    static uint64_t detour(uintptr_t object, uintptr_t factor, uintptr_t b, uintptr_t c, uintptr_t e, uintptr_t f,
                           uintptr_t g, uintptr_t h) {
        noteRemoval(object, factor);
        return original(object, factor, b, c, e, f, g, h);
    }
};

uintptr_t objectById(uintptr_t mgr, uint32_t id) { return reinterpret_cast<ObjectByIdFn>(ds2::at(kObjectById))(mgr, id); }

// Guest, simulation thread: builds the host's structure the way the game builds the player's own.
void create(uintptr_t mgr, const struct_wire::Placed& placed) {
    const struct_wire::Create& c = placed.create;
    if (objectById(mgr, c.id)) {
        logger::write("structures: id %u already exists, not built again", c.id);
        return;
    }
    const uintptr_t desc = reinterpret_cast<FactoryFn>(ds2::at(kFactory))(mgr, c.kind);
    if (!desc) {
        logger::write("structures: the factory refused kind %u", c.kind);
        return;
    }
    alignas(16) uint8_t transform[struct_wire::kTransformBytes];
    std::memcpy(transform, c.transform, sizeof(transform));
    reinterpret_cast<InitFn>(ds2::at(kInit))(desc, transform, kOriginPlayer, c.id);
    ds2::field<uint8_t>(desc, kDescSubKind) = c.subKind;
    ds2::field<uint8_t>(desc, kDescLevel) = c.level;
    std::memcpy(reinterpret_cast<void*>(desc + kDescGuid), c.guid, sizeof(c.guid));
    ds2::field<float>(desc, kDescDurability) = c.durability;
    ds2::field<uint16_t>(desc, kDescFlags) = kPlayerBuildFlags;
    ds2::field<uint32_t>(desc, kDescOwner) = kPlayerOwner;
    std::memcpy(reinterpret_cast<void*>(desc + struct_wire::kBaseDescriptorBytes), placed.tail.data(), placed.tail.size());
    reinterpret_cast<SubmitFn>(ds2::at(kSubmit))(mgr, desc);
    logger::write("structures: built kind %u id %u", c.kind, c.id);
}

// The game's own script export for a player removing a structure: it sends the structure's entity the removal request
// the way the held-item collapse does (calling the object's RequestRemove directly crashed the player code that still
// holds the structure).
void remove(uintptr_t mgr, const struct_wire::Remove& r) {
    if (!objectById(mgr, r.id)) {
        logger::write("structures: id %u to remove does not exist here", r.id);
        return;
    }
    reinterpret_cast<RemoveByPlayerFn>(ds2::at(kRemoveByPlayer))(r.id);
    logger::write("structures: asked the game to remove id %u", r.id);
}

// Simulation thread, ahead of the engine's object update.
void applyPending() {
    std::vector<struct_wire::Placed> creates;
    std::vector<struct_wire::Remove> removes;
    {
        std::lock_guard lock(g_mutex);
        if (g_toCreate.empty() && g_toRemove.empty()) return;
        creates.swap(g_toCreate);
        removes.swap(g_toRemove);
    }
    const uintptr_t mgr = manager();
    if (!mgr) return;
    t_applying = true;
    for (const struct_wire::Placed& placed : creates) create(mgr, placed);
    for (const struct_wire::Remove& r : removes) remove(mgr, r);
    t_applying = false;
}

}  // namespace

namespace structures {

void installEarly() {
    hooks::install("structure submit", ds2::at(kSubmit), reinterpret_cast<void*>(&submitDetour),
                   reinterpret_cast<void**>(&g_submit));
    hooks::install("structure request remove", ds2::at(kRequestRemove), reinterpret_cast<void*>(&requestRemoveDetour),
                   reinterpret_cast<void**>(&g_requestRemove));
    hooks::install("ladder request remove", ds2::at(kLadderRequestRemove), reinterpret_cast<void*>(&Override<0>::detour),
                   reinterpret_cast<void**>(&Override<0>::original));
    hooks::install("catapult request remove", ds2::at(kCatapultRequestRemove), reinterpret_cast<void*>(&Override<1>::detour),
                   reinterpret_cast<void**>(&Override<1>::original));
    hooks::install("safety house request remove", ds2::at(kSafetyHouseRequestRemove),
                   reinterpret_cast<void*>(&Override<2>::detour), reinterpret_cast<void**>(&Override<2>::original));
    sim_tick::add(&applyPending, "structures");
}

}  // namespace structures

namespace game {

void setStructureRole(bool host, bool guest) {
    g_host = host;
    g_guest = guest;
    if (!host) {
        std::lock_guard lock(g_mutex);
        g_placed.clear();
        g_removed.clear();
    }
}

std::vector<struct_wire::Placed> takePlacedStructures() {
    std::lock_guard lock(g_mutex);
    std::vector<struct_wire::Placed> out;
    out.swap(g_placed);
    return out;
}

std::vector<struct_wire::Remove> takeRemovedStructures() {
    std::lock_guard lock(g_mutex);
    std::vector<struct_wire::Remove> out;
    out.swap(g_removed);
    return out;
}

void buildStructure(const struct_wire::Placed& placed) {
    std::lock_guard lock(g_mutex);
    if (g_toCreate.size() < kMaxQueued) g_toCreate.push_back(placed);
}

void removeStructure(const struct_wire::Remove& removal) {
    std::lock_guard lock(g_mutex);
    if (g_toRemove.size() < kMaxQueued) g_toRemove.push_back(removal);
}

}  // namespace game
