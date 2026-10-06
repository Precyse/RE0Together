#include "ds2/remote_guards.h"

#include <windows.h>

#include <cstdint>

#include "decima/entity.h"
#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/remote_player.h"
#include "hooks.h"
#include "log.h"
#include "msvc_rtti.h"

namespace {

// The id table of the entity manager is sized for one player; the remote needs a second slot.
constexpr uintptr_t kInitIdTable = 0x140171ac0;  // (table, capacity)
constexpr uintptr_t kEntityManagerGlobal = 0x14623DEB0;
constexpr uintptr_t kPlayerIdTable = 0x20E0;  // in the entity manager
constexpr uint32_t kPlayerSlots = 4;

// DSPlayerParamComponent's art part toggle indexes the art part instances with an index cached by name, unchecked:
// out of range on a body with fewer parts.
constexpr uintptr_t kArtPartToggle = 0x14104dc40;  // (holder, part id)
constexpr uintptr_t kHolderEntries = 0x18;         // per part id: {index cached by name} at id * 0x18 + 0x18
constexpr uintptr_t kEntityArtParts = 0xD8;
constexpr uintptr_t kArtPartInstances = 0xE8;  // count at +0

// Components of the remote that write the shared HUD block (Inventory, Life, Param, UI race with Sam's on two update
// threads), the mesh paint the body has no entry for, and the use prompts that fault once the body moves.
constexpr const char* kSilencedComponents[] = {
    "DSPlayerMeshPaintComponent",
    "DynamicMeshPaintComponent",
    "DSPlayerUIComponent",
    "DSPlayerParamComponent",
    "DSPlayerUseLocationController",
    "DSPlayerInventoryComponent",
    "DSPlayerLifeComponent"};

// The entity's handler list: a type table {u16 type, u32 start:14 count:10} over 16-byte {component, function} entries.
constexpr uintptr_t kTypeTable = 0x8;
constexpr uintptr_t kHandlers = 0x18;
constexpr size_t kTypeEntrySize = 8;
constexpr size_t kHandlerSize = 16;
constexpr uint32_t kStartMask = 0x3FFF;
constexpr int kCountShift = 14;
constexpr uint32_t kCountMask = 0x3FF;

// DSPlayerComponent's per-frame update handler (message 0x14FF, the stance and state update of a player). Run for the remote
// it flickers Sam's bottom-left prompts (found by clearing the component's handler entries one at a time: with this one
// cleared 0 of 60 screenshots lost the "Activate Terminal" prompt, against about 1 in 8 without). The body's pose and
// animation come from the adapter, so the update is not needed.
constexpr uintptr_t kPlayerComponentUpdate = 0x14080ade0;
constexpr const char* kPlayerComponent = "DSPlayerComponent";

// The removal handlers that clear DSPlayerSystem's singleton pointers (see player_system_guard, which diverts the init's
// stores): the remote's removal would clear Sam's.
struct Removal {
    const char* component;
    uintptr_t function;
};
constexpr Removal kSingletonRemovals[] = {{"DSPlayerEquipmentManageComponent", 0x140F7A930},
                                          {"DSPlayerFacialRigManagerComponent", 0x140EE3EE0}};

using InitTableFn = void (*)(uintptr_t table, uint32_t capacity);
using PartToggleFn = void (*)(uintptr_t holder, uint8_t id);
InitTableFn g_initTable = nullptr;
PartToggleFn g_partToggle = nullptr;

void initTableDetour(uintptr_t table, uint32_t capacity) {
    const uintptr_t entityManager = decima::readPointer(ds2::at(kEntityManagerGlobal));
    if (entityManager && table == entityManager + kPlayerIdTable && capacity < kPlayerSlots) {
        logger::write("remote_guards: player id table sized %u instead of %u", kPlayerSlots, capacity);
        capacity = kPlayerSlots;
    }
    g_initTable(table, capacity);
}

void partToggleDetour(uintptr_t holder, uint8_t id) {
    const uintptr_t entity = decima::readPointer(holder);
    if (!entity || entity != remote_player::entity()) {
        g_partToggle(holder, id);  // Sam's: index -1 is a valid "none" there
        return;
    }
    int32_t index = -1;
    int32_t count = 0;
    decima::safeRead(holder + id * kHolderEntries + kHolderEntries, index);
    const uintptr_t instances = decima::readPointer(decima::readPointer(entity + kEntityArtParts) + kArtPartInstances);
    decima::safeRead(instances, count);
    if (index >= 0 && index < count) g_partToggle(holder, id);
}

// Clears the component's entries in the entity's handler list (all of them, or only those that call `onlyFunction`); the
// dispatcher drops cleared entries itself.
void silence(uintptr_t entity, uintptr_t component, uintptr_t onlyFunction = 0) {
    const uintptr_t list = entity + ds2::kEntityHandlerList;
    const int32_t types = ds2::field<int32_t>(list, 0);
    const uintptr_t table = decima::readPointer(list + kTypeTable);
    const uintptr_t handlers = decima::readPointer(list + kHandlers);
    for (int32_t t = 0; component && t < types; ++t) {
        const uint32_t packed = ds2::field<uint32_t>(table + t * kTypeEntrySize, sizeof(uint32_t));
        const uint32_t first = packed & kStartMask;
        const uint32_t count = (packed >> kCountShift) & kCountMask;
        for (uint32_t i = first; i < first + count; ++i) {
            auto& entry = ds2::field<uintptr_t>(handlers + i * kHandlerSize, 0);
            const uintptr_t function = ds2::field<uintptr_t>(handlers + i * kHandlerSize, sizeof(uintptr_t));
            if ((entry & ~uintptr_t{1}) == component && (!onlyFunction || function == onlyFunction)) entry &= 1;  // keeps the low flag bit
        }
    }
}

}  // namespace

namespace remote_guards {

void installEarly() {
    hooks::install("id table init", ds2::at(kInitIdTable), reinterpret_cast<void*>(&initTableDetour),
                   reinterpret_cast<void**>(&g_initTable));
    hooks::install("art part toggle", ds2::at(kArtPartToggle), reinterpret_cast<void*>(&partToggleDetour),
                   reinterpret_cast<void**>(&g_partToggle));
}

void silenceRemote() {
    const uintptr_t entity = remote_player::entity();
    for (const char* name : kSilencedComponents) {
        silence(entity, decima::findComponent(entity, msvc_rtti::vtableOf(name)));
    }
    silence(entity, decima::findComponent(entity, msvc_rtti::vtableOf(kPlayerComponent)), ds2::at(kPlayerComponentUpdate));
    for (const Removal& removal : kSingletonRemovals) {
        silence(entity, decima::findComponent(entity, msvc_rtti::vtableOf(removal.component)), ds2::at(removal.function));
    }
}

}  // namespace remote_guards
