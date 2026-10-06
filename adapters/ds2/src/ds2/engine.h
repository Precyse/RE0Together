#pragma once
// DS2-internal: addresses and object access shared by the ds2/ files. The addresses are file virtual addresses of the
// shipping DS2.exe (image base 0x140000000), rebased onto the live module.
#include <windows.h>

#include <cstdint>

#include "decima/safe_read.h"

namespace ds2 {

constexpr uintptr_t kImageBase = 0x140000000;

inline uintptr_t at(uintptr_t fileVa) {
    return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + (fileVa - kImageBase);
}

constexpr uintptr_t kImageSpan = 0x20000000;  // more than the game image's size

// Whether an address lies inside the game's image (code, vtables, globals): a pointer read from freed or reused memory
// that is called through must pass this first.
inline bool inGameImage(uintptr_t address) {
    const uintptr_t base = at(kImageBase);
    return address >= base && address - base < kImageSpan;
}

// A field of a live engine object.
template <class T>
T& field(uintptr_t object, uintptr_t offset) {
    return *reinterpret_cast<T*>(object + offset);
}

constexpr uintptr_t kEntityTransform = 0xE8;    // world transform
constexpr uintptr_t kEntityParent = 0x80;       // the entity it is attached to, 0 when free
constexpr uintptr_t kEntityHandlerList = 0x2D0;
constexpr uintptr_t kEntityNetworkId = 0x320;   // the id vehicles and drivers are keyed by
constexpr uintptr_t kEntityController = 0x5658; // DSPlayerController
constexpr uintptr_t kEntityUuid = 0x10;         // ObjectUUID, 16 bytes
constexpr uintptr_t kEntityWeakTarget = 0x20;   // a weak pointer to an entity points at entity + 0x20

// The entity the weak pointer stored at `slot` refers to, or 0.
inline uintptr_t weakEntity(uintptr_t slot) {
    const uintptr_t target = decima::readPointer(slot);
    return target ? target - kEntityWeakTarget : 0;
}

// A component of an entity by its Decima type record (file VA).
inline uintptr_t componentByRecord(uintptr_t entity, uintptr_t recordFileVa) {
    constexpr uintptr_t kGetComponent = 0x14011ffa0;  // (entity + 0xA0, type record)
    constexpr uintptr_t kEntityComponents = 0xA0;
    using GetComponentFn = uintptr_t (*)(uintptr_t components, uintptr_t record);
    return reinterpret_cast<GetComponentFn>(at(kGetComponent))(entity + kEntityComponents, at(recordFileVa));
}

}  // namespace ds2
