#include "ds2/setdriver_guard.h"

#include <atomic>
#include <cstdint>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/remote_context.h"
#include "ds2/remote_player.h"
#include "game.h"
#include "vehicle_sync.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kSetDriver = 0x141f6bde0;  // (vehicle, const u64* driverKey, bool enter, IVehicleDriver*, bool)
constexpr uintptr_t kVehicleManagerGlobal = 0x14623FA20;
constexpr uintptr_t kLocalDriving = 0x79E0;  // block inside DSVehicleManager
constexpr uintptr_t kDrivenVehicle = 0xF8;   // entity the local player drives, 0 on foot
constexpr uintptr_t kLastDrivenId = 0x110;
constexpr uintptr_t kLastDrivenId2 = 0x118;
constexpr uintptr_t kVehicleListCount = 0x8;
constexpr uintptr_t kVehicleListCount2 = 0xA0;
constexpr uintptr_t kVehicleHasDriver = 0x4fc;  // non-zero while the vehicle has a driver
constexpr uintptr_t kVehicleDriverKey = 0x4b0;  // that driver's key
constexpr uintptr_t kVehicleFreeGate = 0x140beec80;  // (vehicle) -> bool: free to use (the Use Vehicle prompt and its searches)

using SetDriverFn = bool (*)(uintptr_t vehicle, const uint64_t* key, bool enter, uintptr_t driver, bool flag);
SetDriverFn g_setDriver = nullptr;
using FreeGateFn = bool (*)(uintptr_t vehicle);
FreeGateFn g_freeGate = nullptr;
std::atomic<uint64_t> g_samPassengerVehicle{0};
bool g_loggedEnter = false;
bool g_loggedLeave = false;

// DS2 has no passenger seat for players: a second rider's SetDriver would fail every frame because the vehicle's
// one driver slot is taken. The remote's enter and leave are answered as done instead, and the vehicle keeps its
// driver; the remote's ride states finish and it sits as a passenger.
bool answersAsPassenger(uintptr_t vehicle, const uint64_t* key, bool enter) {
    const bool taken = ds2::field<uint32_t>(vehicle, kVehicleHasDriver) != 0 &&
                       ds2::field<uint64_t>(vehicle, kVehicleDriverKey) != *key;
    bool& logged = enter ? g_loggedEnter : g_loggedLeave;
    if (taken && !logged) logger::write("setdriver_guard: remote %s as a passenger", enter ? "enters" : "leaves");
    logged = taken;
    return taken;
}

// The vehicle the partner reports driving, if it is this one.
bool partnerDrives(uintptr_t vehicle) {
    const auto riding = vehicle_sync::partnerRiding(remote_player::slot());
    return riding && riding->role == vehicle_sync::kRoleDriver &&
           riding->id == ds2::field<uint64_t>(vehicle, ds2::kEntityNetworkId);
}

// The "Use Vehicle" prompt is red while the vehicle has a driver. It is let through for the vehicle the partner drives
// when the local player is on foot, so he can sit in the second pod.
bool freeGateDetour(uintptr_t vehicle) {
    if (g_freeGate(vehicle)) return true;
    return vehicle && !game::drivenVehicle() && g_samPassengerVehicle.load() == 0 && partnerDrives(vehicle);
}

bool setDriverDetour(uintptr_t vehicle, const uint64_t* key, bool enter, uintptr_t driver, bool flag) {
    const uintptr_t sam = remote_player::samEntity();
    if (sam && key && *key == ds2::field<uint64_t>(sam, ds2::kEntityNetworkId) && !remote_context::active() &&
        ds2::field<uint32_t>(vehicle, kVehicleHasDriver) != 0 && ds2::field<uint64_t>(vehicle, kVehicleDriverKey) != *key) {
        // The partner holds the driver slot: Sam's enter or leave is answered as done, he never becomes the driver.
        g_samPassengerVehicle = enter ? ds2::field<uint64_t>(vehicle, ds2::kEntityNetworkId) : 0;
        logger::write("setdriver_guard: the local player %s as a passenger", enter ? "enters" : "leaves");
        return true;
    }
    const uintptr_t remote = remote_player::entity();
    const bool remoteKey = remote && key && *key == ds2::field<uint64_t>(remote, ds2::kEntityNetworkId);
    const bool isRemote = remoteKey || remote_context::active();
    const uintptr_t manager = decima::readPointer(ds2::at(kVehicleManagerGlobal));
    if (!isRemote || !manager) return g_setDriver(vehicle, key, enter, driver, flag);
    if (remoteKey && answersAsPassenger(vehicle, key, enter)) return true;

    const uintptr_t block = manager + kLocalDriving;
    const uintptr_t driven = ds2::field<uintptr_t>(block, kDrivenVehicle);
    const uint64_t last = ds2::field<uint64_t>(block, kLastDrivenId);
    const uint64_t last2 = ds2::field<uint64_t>(block, kLastDrivenId2);
    const int32_t count = ds2::field<int32_t>(block, kVehicleListCount);
    const int32_t count2 = ds2::field<int32_t>(block, kVehicleListCount2);
    const bool result = g_setDriver(vehicle, key, enter, driver, flag);
    ds2::field<uintptr_t>(block, kDrivenVehicle) = driven;
    ds2::field<uint64_t>(block, kLastDrivenId) = last;
    ds2::field<uint64_t>(block, kLastDrivenId2) = last2;
    ds2::field<int32_t>(block, kVehicleListCount) = count;
    ds2::field<int32_t>(block, kVehicleListCount2) = count2;
    logger::write("setdriver_guard: remote %s vehicle %p, kept local driven %p", enter ? "entered" : "left",
                  reinterpret_cast<void*>(vehicle), reinterpret_cast<void*>(driven));
    return result;
}

}  // namespace

namespace setdriver_guard {

void install() {
    hooks::install("vehicle set driver", ds2::at(kSetDriver), reinterpret_cast<void*>(&setDriverDetour),
                   reinterpret_cast<void**>(&g_setDriver));
    hooks::install("vehicle free gate", ds2::at(kVehicleFreeGate), reinterpret_cast<void*>(&freeGateDetour),
                   reinterpret_cast<void**>(&g_freeGate));
}

uint64_t localPassengerVehicle() { return g_samPassengerVehicle.load(); }

void endLocalPassenger() { g_samPassengerVehicle = 0; }

}  // namespace setdriver_guard
