// DEATH STRANDING 2: puppet bodies for remote players. A humanoid NPC the game has already loaded (an entity whose
// mover is a DSNpcGroundMover, that has a model and is not an animal) is found by a background scan of the game's
// heaps, borrowed and moved with Entity::SetWorldTransform (entity lock, copy, dirty flag) on the simulation thread. Creating a fresh entity (EntityResource::CreateEntity) faulted in component setup with
// both the player's and an NPC's resource; see docs/DS2_NOTES.md.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

#include "decima/safe_read.h"
#include "decima/world_transform.h"
#include "ds2/player.h"
#include "game.h"
#include "log.h"
#include "msvc_rtti.h"
#include "pattern_scan.h"

namespace {

// Entity::SetWorldTransform (script export Entity_ExportedSetWorldTransform).
constexpr const char* kSetWorldTransform =
    "48 85 C9 74 1A 53 48 83 EC 20 48 8B D9 E8 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 48 83 C4 20 5B C3";

constexpr const char* kNpcMoverClass = "DSNpcGroundMover";  // the mover of walking NPCs (people and animals)
constexpr const char* kAnimalClass = "DSAnimalComponent";    // present on animals only
constexpr uintptr_t kComponentOwner = 0x48;     // a component's entity (live: mover +0x48, matching Entity.Mover)
constexpr uintptr_t kEntityMover = 0xC0;        // Entity.Mover, RTTI
constexpr uintptr_t kEntityTransform = 0xE8;    // Entity.Orientation (WorldTransform), RTTI
constexpr uintptr_t kEntityModel = 0xC8;        // Entity.Model, RTTI
constexpr uintptr_t kComponentCount = 0xA0;     // Entity.Components (EntityComponentContainer): u32 count
constexpr uintptr_t kComponentArray = 0xA8;     // ... and the component pointer array
constexpr uint32_t kMaxComponents = 256;
constexpr size_t kScanBlock = 1 << 20;

using SetWorldTransformFn = void (*)(uintptr_t entity, const decima::WorldTransform* transform);

// Mover::SetVelocity, the virtual Entity_ExportedSetVelocity forwards to (slot 0xC8 / 8).
constexpr size_t kMoverSetVelocitySlot = 0xC8 / sizeof(void*);
struct Velocity {
    float x, y, z, w;
};
using SetVelocityFn = void (*)(uintptr_t mover, const Velocity* velocity);

SetWorldTransformFn g_setWorldTransform = nullptr;

decima::WorldTransform transformOf(const game::Pose& pose) {
    const float s = std::sin(pose.yaw), c = std::cos(pose.yaw);
    decima::WorldTransform t{};
    t.position = {pose.position.x, pose.position.y, pose.position.z};
    t.orientation.row[0][0] = c;  // right
    t.orientation.row[0][1] = -s;
    t.orientation.row[1][0] = s;  // forward
    t.orientation.row[1][1] = c;
    t.orientation.row[2][2] = 1;  // up
    return t;
}

bool guardedPlace(uintptr_t body, const decima::WorldTransform* t, const Velocity* v) {
    __try {
        g_setWorldTransform(body, t);
        const uintptr_t mover = *reinterpret_cast<const uintptr_t*>(body + kEntityMover);
        if (mover) {
            const auto setVelocity = (*reinterpret_cast<SetVelocityFn* const*>(mover))[kMoverSetVelocitySlot];
            setVelocity(mover, v);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The owner of a mover found in memory, if it really is that entity's mover.
uintptr_t ownerOfMover(uintptr_t mover) {
    const uintptr_t entity = decima::readPointer(mover + kComponentOwner);
    return entity && decima::readPointer(entity + kEntityMover) == mover ? entity : 0;
}

bool hasComponent(uintptr_t entity, uintptr_t vtable) {
    uint32_t count = 0;
    const uintptr_t array = decima::readPointer(entity + kComponentArray);
    if (!decima::safeRead(entity + kComponentCount, count) || !array || count > kMaxComponents) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (decima::readPointer(decima::readPointer(array + i * sizeof(uintptr_t))) == vtable) return true;
    }
    return false;
}

double distanceSquared(uintptr_t a, uintptr_t b) {
    decima::WorldPosition pa{}, pb{};
    if (!decima::safeRead(a + kEntityTransform, pa) || !decima::safeRead(b + kEntityTransform, pb)) return 1e30;
    const double dx = pa.x - pb.x, dy = pa.y - pb.y, dz = pa.z - pb.z;
    return dx * dx + dy * dy + dz * dz;
}

// Scans every private read-write region for NPC movers and returns the nearest rendered non-animal owner.
uintptr_t findHumanoidNpc() {
    const uintptr_t moverVtable = msvc_rtti::vtableOf(kNpcMoverClass);
    const uintptr_t animalVtable = msvc_rtti::vtableOf(kAnimalClass);
    const uintptr_t player = ds2::localPlayerEntity();
    if (!moverVtable || !player) return 0;
    uintptr_t best = 0;
    double bestDistance = 0;
    std::vector<uintptr_t> block(kScanBlock / sizeof(uintptr_t));
    MEMORY_BASIC_INFORMATION info{};
    for (uintptr_t at = 0; VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof(info)); at += info.RegionSize) {
        if (info.State != MEM_COMMIT || info.Type != MEM_PRIVATE || info.Protect != PAGE_READWRITE) continue;
        for (size_t off = 0; off < info.RegionSize; off += kScanBlock) {
            const size_t size = std::min(kScanBlock, info.RegionSize - off);
            if (!decima::safeCopy(block.data(), at + off, size)) continue;
            for (size_t i = 0; i < size / sizeof(uintptr_t); ++i) {
                if (block[i] != moverVtable) continue;
                const uintptr_t entity = ownerOfMover(at + off + i * sizeof(uintptr_t));
                if (!entity || !decima::readPointer(entity + kEntityModel) || hasComponent(entity, animalVtable)) continue;
                const double distance = distanceSquared(entity, player);
                if (!best || distance < bestDistance) best = entity, bestDistance = distance;
            }
        }
    }
    return best;
}

enum class Search { Idle, Running, Done };
std::atomic<Search> g_search{Search::Idle};
std::atomic<uintptr_t> g_found{0};

}  // namespace

namespace game {

std::optional<Body> borrowBody() {
    Search expected = Search::Idle;
    if (g_search.compare_exchange_strong(expected, Search::Running)) {
        // The scan reads gigabytes; it runs on its own thread so no game thread stalls.
        std::thread([] {
            g_setWorldTransform = reinterpret_cast<SetWorldTransformFn>(pattern_scan::find(kSetWorldTransform));
            g_found = g_setWorldTransform ? findHumanoidNpc() : 0;
            logger::write("body: humanoid NPC %p (SetWorldTransform %p)", reinterpret_cast<void*>(g_found.load()),
                          reinterpret_cast<void*>(g_setWorldTransform));
            g_search = Search::Done;
        }).detach();
    }
    if (g_search.load() != Search::Done) return std::nullopt;
    g_search = Search::Idle;  // a later borrow searches again (NPCs stream in and out)
    return g_found.load();
}

std::optional<Pose> bodyPose(Body body) {
    decima::WorldTransform t;
    if (!body || !decima::safeRead(body + kEntityTransform, t)) return std::nullopt;
    const float* forward = t.orientation.row[1];
    return Pose{{t.position.x, t.position.y, t.position.z}, std::atan2(forward[0], forward[1])};
}

bool placeBody(Body body, const Pose& pose, const world_to_screen::Vec3& velocity) {
    const decima::WorldTransform t = transformOf(pose);
    const Velocity v{static_cast<float>(velocity.x), static_cast<float>(velocity.y), static_cast<float>(velocity.z), 0};
    return body && g_setWorldTransform && guardedPlace(body, &t, &v);
}

}  // namespace game
