#include "ds2/entity_lookup.h"

#include <array>

#include "decima/safe_read.h"
#include "ds2/engine.h"

namespace {

constexpr uintptr_t kEntityManagerGlobal = 0x14623DEB0;  // EntityManagerGame
constexpr uintptr_t kEntityMap = 0x398;                  // UUID -> entity map
constexpr uintptr_t kFindByUuid = 0x1401846d0;           // (map*, const GGUUID*) -> slot index, -1 when absent
constexpr uintptr_t kGetByUuid = 0x140178320;            // EntityManager::GetEntityByUUID(unused, const GGUUID*) -> Entity* or 0
constexpr int32_t kAbsent = -1;
constexpr uintptr_t kEntityUuid = 0x10;
constexpr uintptr_t kEntityFlags = 0x98;
constexpr uint64_t kDeadFlag = uint64_t{1} << 8;

using FindFn = int32_t (*)(uintptr_t map, const void* uuid);
using GetByUuidFn = uintptr_t (*)(uintptr_t unused, const void* uuid);

}  // namespace

namespace ds2 {

uintptr_t entityManager() { return decima::readPointer(at(kEntityManagerGlobal)); }

bool entityExists(const uint8_t* uuid) {
    const uintptr_t manager = entityManager();
    if (!manager) return false;
    alignas(16) std::array<uint8_t, 16> key;
    std::copy(uuid, uuid + key.size(), key.begin());
    return reinterpret_cast<FindFn>(at(kFindByUuid))(manager + kEntityMap, key.data()) != kAbsent;
}

uintptr_t entityByUuid(const uint8_t* uuid) {
    if (!entityManager()) return 0;
    alignas(16) std::array<uint8_t, 16> key;
    std::copy(uuid, uuid + key.size(), key.begin());
    return reinterpret_cast<GetByUuidFn>(at(kGetByUuid))(0, key.data());
}

bool entityUuid(uintptr_t entity, std::array<uint8_t, 16>& out) {
    return decima::safeCopy(out.data(), entity + kEntityUuid, out.size());
}

bool entityIsDead(uintptr_t entity) {
    uint64_t flags = 0;
    return !decima::safeRead(entity + kEntityFlags, flags) || (flags & kDeadFlag) != 0;
}

}  // namespace ds2
