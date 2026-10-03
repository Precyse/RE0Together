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

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

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

void GameplayClock::update(uintptr_t playerEntity) {
    if (!inGameplay(playerEntity)) {
        m_since = kInactive;
        return;
    }
    int64_t expected = kInactive;
    m_since.compare_exchange_strong(expected, nowMs());
}

bool GameplayClock::settled() const {
    const int64_t since = m_since.load();
    return since != kInactive &&
           nowMs() - since >= std::chrono::duration_cast<std::chrono::milliseconds>(kGameplaySettle).count();
}

}  // namespace ds2
