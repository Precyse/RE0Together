#include "ds2/player_state.h"

#include "decima/entity.h"
#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "msvc_rtti.h"

namespace {

constexpr const char* kPlayerComponent = "DSPlayerComponent";
constexpr uintptr_t kComponentState = 0x1b8;    // DSPlayerComponent -> its DSPlayerState
constexpr uintptr_t kStateRidePlugin = 0x718;
constexpr uintptr_t kPluginParent = 0x10;       // the plugin it depends on: the core action plugin
constexpr uintptr_t kPluginActive = 0x8;

}  // namespace

namespace ds2 {

uintptr_t ridePlugin(uintptr_t playerEntity) {
    const uintptr_t component = decima::findComponent(playerEntity, msvc_rtti::vtableOf(kPlayerComponent));
    const uintptr_t state = component ? decima::readPointer(component + kComponentState) : 0;
    return state ? decima::readPointer(state + kStateRidePlugin) : 0;
}

bool inGameplay(uintptr_t playerEntity) {
    const uintptr_t plugin = ridePlugin(playerEntity);
    const uintptr_t core = plugin ? decima::readPointer(plugin + kPluginParent) : 0;
    return core && field<uint8_t>(core, kPluginActive) != 0;
}

}  // namespace ds2
