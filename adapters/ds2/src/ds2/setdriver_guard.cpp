#include "ds2/setdriver_guard.h"

#include <cstdint>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/remote_context.h"
#include "ds2/remote_player.h"
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

using SetDriverFn = bool (*)(uintptr_t vehicle, const uint64_t* key, bool enter, uintptr_t driver, bool flag);
SetDriverFn g_setDriver = nullptr;

bool setDriverDetour(uintptr_t vehicle, const uint64_t* key, bool enter, uintptr_t driver, bool flag) {
    const uintptr_t remote = remote_player::entity();
    const bool remoteKey = remote && key && *key == ds2::field<uint64_t>(remote, ds2::kEntityNetworkId);
    const bool isRemote = remoteKey || remote_context::active();
    const uintptr_t manager = decima::readPointer(ds2::at(kVehicleManagerGlobal));
    if (!isRemote || !manager) return g_setDriver(vehicle, key, enter, driver, flag);

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
}

}  // namespace setdriver_guard
