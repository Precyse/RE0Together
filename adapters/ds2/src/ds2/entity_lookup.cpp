#include "ds2/entity_lookup.h"

#include <array>

#include "decima/safe_read.h"
#include "ds2/engine.h"

namespace {

constexpr uintptr_t kEntityManagerGlobal = 0x14623DEB0;  // EntityManagerGame
constexpr uintptr_t kEntityMap = 0x398;                  // UUID -> entity map
constexpr uintptr_t kFindByUuid = 0x1401846d0;           // (map*, const GGUUID*) -> slot index, -1 when absent
constexpr int32_t kAbsent = -1;

using FindFn = int32_t (*)(uintptr_t map, const void* uuid);

}  // namespace

namespace ds2 {

bool entityExists(const uint8_t* uuid) {
    const uintptr_t manager = decima::readPointer(at(kEntityManagerGlobal));
    if (!manager) return false;
    alignas(16) std::array<uint8_t, 16> key;
    std::copy(uuid, uuid + key.size(), key.begin());
    return reinterpret_cast<FindFn>(at(kFindByUuid))(manager + kEntityMap, key.data()) != kAbsent;
}

}  // namespace ds2
