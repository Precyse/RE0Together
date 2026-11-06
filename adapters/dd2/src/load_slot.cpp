#include "load_slot.h"

#include <atomic>
#include <cstdint>

#include "hook_registry.h"
#include "log.h"
#include "names.h"

namespace {

// REFramework hook arguments: argv[0] the VM context, argv[1] `this`, then the method's parameters, 8 bytes each.
constexpr int kSlotParam = 2;

std::atomic<int> g_slot{game_state_rule::kSlotUnknown};

int preRequestLoad(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    if (argc <= kSlotParam) return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    const int slot = static_cast<int32_t>(reinterpret_cast<intptr_t>(argv[kSlotParam]));
    if (slot < 0) return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    if (g_slot.exchange(slot) != slot) logger::write("load_slot: the game loads slot %d", slot);
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

}  // namespace

namespace load_slot {

bool install() {
    const hook_registry::Handler handlers[] = {hook_registry::make<preRequestLoad>(&names::kRequestLoad)};
    return hook_registry::installSet(names::kLoadSlot, handlers);
}

int current() { return g_slot; }

}  // namespace load_slot
