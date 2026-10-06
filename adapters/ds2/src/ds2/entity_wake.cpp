// DEATH STRANDING 2: waking a sleeping entity (see ds2/entity_wake.h).
#include "ds2/entity_wake.h"

#include <windows.h>

#include "decima/safe_read.h"
#include "ds2/engine.h"

namespace {

constexpr uintptr_t kEntityFlags = 0x98;
constexpr unsigned kAsleepBit = 9;
constexpr uintptr_t kWakeEntity = 0x1401312b0;

}  // namespace

namespace ds2 {

bool entityAsleep(uintptr_t entity) {
    uint64_t flags = 0;
    return decima::safeRead(entity + kEntityFlags, flags) && ((flags >> kAsleepBit) & 1);
}

bool wakeEntity(uintptr_t entity) {
    __try {
        reinterpret_cast<void (*)(uintptr_t, uintptr_t, uintptr_t)>(at(kWakeEntity))(entity, 0, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace ds2
