#pragma once
// DS2-internal: the layout of the NPC managers (DSSneakingNpcManager and two siblings) that the activity update 0x141bd3d50
// reads, shared by the patch (ds2/sneaking_focus.cpp) and its diagnostics (ds2/sneaking_diag.cpp).
#include <cstdint>

#include "decima/safe_read.h"

namespace sneaking_groups {

// Per-group arrays of the manager, indexed by group.
constexpr uintptr_t kBudgets = 0x30;  // uint32*: awake budget
constexpr uintptr_t kNear = 0x40;     // float*: near radius
constexpr uintptr_t kFar = 0x50;      // float*: far radius
constexpr uintptr_t kLimit = 0x60;    // int32*: wakes per update (the update uses 1000 when negative)

constexpr uintptr_t kGroupCount = 0x138;  // int32
constexpr uintptr_t kGroups = 0x140;      // Group*
constexpr uintptr_t kItemEntity = 0x88;   // Item +0x88: the member's Entity*

// A group's record: its members.
struct Group {
    int32_t count;
    int32_t pad;
    uintptr_t* items;
};
static_assert(sizeof(Group) == 0x10);

// The member's entity, 0 when unreadable.
inline uintptr_t memberEntity(uintptr_t item) {
    uintptr_t entity = 0;
    return decima::safeRead(item + kItemEntity, entity) ? entity : 0;
}

}  // namespace sneaking_groups
