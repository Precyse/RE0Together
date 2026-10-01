#include "ds2/place.h"

#include <windows.h>

#include "decima/safe_read.h"
#include "log.h"
#include "pattern_scan.h"

namespace {

// Entity::SetWorldTransform (script export Entity_ExportedSetWorldTransform).
constexpr const char* kSetWorldTransform =
    "48 85 C9 74 1A 53 48 83 EC 20 48 8B D9 E8 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 48 83 C4 20 5B C3";
constexpr uintptr_t kEntityMover = 0xC0;      // Entity.Mover, RTTI
constexpr uintptr_t kEntityTransform = 0xE8;  // Entity.Orientation (WorldTransform), RTTI
// Mover::SetVelocity, the virtual Entity_ExportedSetVelocity forwards to (slot 0xC8 / 8).
constexpr size_t kMoverSetVelocitySlot = 0xC8 / sizeof(void*);

struct Velocity {
    float x, y, z, w;
};
using SetWorldTransformFn = void (*)(uintptr_t entity, const decima::WorldTransform* transform);
using SetVelocityFn = void (*)(uintptr_t mover, const Velocity* velocity);

SetWorldTransformFn setWorldTransform() {
    static const auto found = [] {
        const auto fn = reinterpret_cast<SetWorldTransformFn>(pattern_scan::find(kSetWorldTransform));
        logger::write("place: SetWorldTransform %p", reinterpret_cast<void*>(fn));
        return fn;
    }();
    return found;
}

bool guardedPlace(SetWorldTransformFn set, uintptr_t entity, const decima::WorldTransform* t, const Velocity* v) {
    __try {
        set(entity, t);
        const uintptr_t mover = *reinterpret_cast<const uintptr_t*>(entity + kEntityMover);
        if (mover) {
            const auto setVelocity = (*reinterpret_cast<SetVelocityFn* const*>(mover))[kMoverSetVelocitySlot];
            setVelocity(mover, v);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

namespace ds2 {

bool placeEntity(uintptr_t entity, const decima::WorldTransform& transform, const world_to_screen::Vec3& velocity) {
    const SetWorldTransformFn set = setWorldTransform();
    const Velocity v{static_cast<float>(velocity.x), static_cast<float>(velocity.y), static_cast<float>(velocity.z), 0};
    return entity && set && guardedPlace(set, entity, &transform, &v);
}

bool entityTransform(uintptr_t entity, decima::WorldTransform& out) {
    return entity && decima::safeRead(entity + kEntityTransform, out);
}

}  // namespace ds2
