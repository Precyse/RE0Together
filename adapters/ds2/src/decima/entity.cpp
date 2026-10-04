#include "decima/entity.h"

#include "decima/safe_read.h"

namespace {

constexpr uintptr_t kComponentCount = 0xA0;  // Entity.Components (EntityComponentContainer): u32 count
constexpr uintptr_t kComponentArray = 0xA8;  // ... and the component pointer array
constexpr uint32_t kMaxComponents = 256;

}  // namespace

namespace decima {

uintptr_t findComponentWhere(uintptr_t entity, const std::function<bool(uintptr_t component)>& matches) {
    uint32_t count = 0;
    const uintptr_t array = readPointer(entity + kComponentArray);
    if (!safeRead(entity + kComponentCount, count) || !array || count > kMaxComponents) return 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uintptr_t component = readPointer(array + i * sizeof(uintptr_t));
        if (component && matches(component)) return component;
    }
    return 0;
}

uintptr_t findComponent(uintptr_t entity, uintptr_t vtable) {
    return findComponentWhere(entity, [vtable](uintptr_t component) { return readPointer(component) == vtable; });
}

}  // namespace decima
