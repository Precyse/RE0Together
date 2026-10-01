// DEATH STRANDING 2: puppet bodies for remote players. A humanoid NPC the game has already loaded (an entity whose
// mover is a DSNpcGroundMover) is borrowed and moved with Entity::SetWorldTransform (entity lock, copy, dirty flag)
// on the simulation thread. Creating a fresh entity (EntityResource::CreateEntity) faulted in component setup with
// both the player's and an NPC's resource; see docs/DS2_NOTES.md.
#include <windows.h>

#include <algorithm>
#include <cmath>
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

constexpr const char* kNpcMoverClass = "DSNpcGroundMover";  // the mover of walking humanoid NPCs
constexpr uintptr_t kComponentOwner = 0x48;     // a component's entity (live: mover +0x48, matching Entity.Mover)
constexpr uintptr_t kEntityMover = 0xC0;        // Entity.Mover, RTTI
constexpr uintptr_t kEntityTransform = 0xE8;    // Entity.Orientation (WorldTransform), RTTI
constexpr size_t kScanBlock = 1 << 20;

using SetWorldTransformFn = void (*)(uintptr_t entity, const decima::WorldTransform* transform);

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

bool guardedPlace(uintptr_t body, const decima::WorldTransform* t) {
    __try {
        g_setWorldTransform(body, t);
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

// Entities share the player entity's heap: scan that allocation for an NPC mover and return its entity.
uintptr_t findNpc() {
    const uintptr_t moverVtable = msvc_rtti::vtableOf(kNpcMoverClass);
    const uintptr_t player = ds2::localPlayerEntity();
    MEMORY_BASIC_INFORMATION info{};
    if (!moverVtable || !player || !VirtualQuery(reinterpret_cast<void*>(player), &info, sizeof(info))) return 0;
    const void* allocation = info.AllocationBase;
    std::vector<uintptr_t> block(kScanBlock / sizeof(uintptr_t));
    for (auto at = reinterpret_cast<uintptr_t>(allocation);
         VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof(info)) && info.AllocationBase == allocation;
         at += info.RegionSize) {
        if (info.State != MEM_COMMIT || info.Protect != PAGE_READWRITE) continue;
        for (size_t off = 0; off < info.RegionSize; off += kScanBlock) {
            const size_t size = std::min(kScanBlock, info.RegionSize - off);
            if (!decima::safeCopy(block.data(), at + off, size)) continue;
            for (size_t i = 0; i < size / sizeof(uintptr_t); ++i) {
                if (block[i] != moverVtable) continue;
                if (const uintptr_t entity = ownerOfMover(at + off + i * sizeof(uintptr_t))) return entity;
            }
        }
    }
    return 0;
}

}  // namespace

namespace game {

Body borrowBody() {
    if (!g_setWorldTransform) {
        g_setWorldTransform = reinterpret_cast<SetWorldTransformFn>(pattern_scan::find(kSetWorldTransform));
    }
    const uintptr_t npc = g_setWorldTransform ? findNpc() : 0;
    logger::write("body: borrowed NPC %p (SetWorldTransform %p)", reinterpret_cast<void*>(npc),
                  reinterpret_cast<void*>(g_setWorldTransform));
    return npc;
}

std::optional<Pose> bodyPose(Body body) {
    decima::WorldTransform t;
    if (!body || !decima::safeRead(body + kEntityTransform, t)) return std::nullopt;
    const float* forward = t.orientation.row[1];
    return Pose{{t.position.x, t.position.y, t.position.z}, std::atan2(forward[0], forward[1])};
}

bool placeBody(Body body, const Pose& pose) {
    const decima::WorldTransform t = transformOf(pose);
    return body && g_setWorldTransform && guardedPlace(body, &t);
}

}  // namespace game
