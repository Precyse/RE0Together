#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>

// DS2-internal: a player entity's DSPlayerState, the engine's humanoid state object (reached through the entity's
// DSPlayerComponent), and the action plugins it holds.
namespace ds2 {

// The DSPlayerRideVehicleActionPlugin of the player entity, or 0.
uintptr_t ridePlugin(uintptr_t playerEntity);

// Whether the player entity's state machine is running, which it is only once gameplay has started: while the world is
// still loading the entity already exists, but its core action plugin is idle.
bool inGameplay(uintptr_t playerEntity);

// How long the local player's state machine has stayed active. It turns active about 3 s after Continue, while the
// title screen is still up and the world loads, and inactive again on the way back to the title, so "gameplay" is
// taken to have started only once it has stayed active for `kGameplaySettle`. Safe to update from several threads.
class GameplayClock {
public:
    static constexpr std::chrono::seconds kGameplaySettle{8};

    void update(uintptr_t playerEntity);
    bool active() const { return m_since.load() != kInactive; }
    bool settled() const;

private:
    static constexpr int64_t kInactive = -1;  // steady-clock milliseconds since the machine became active
    std::atomic<int64_t> m_since{kInactive};
};

}  // namespace ds2
