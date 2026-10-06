// DEATH STRANDING 2: the camps' stealth NPCs (DSSneakingNpcManager and two sibling managers) are put to sleep by their own
// activity update, 0x141bd3d50 (called from the managers' update slots). Each call copies ONE position, EntityManagerGame
// +0xA0 (this machine's player), measures every member of every group to it against the group's near and far radii and
// puts what is beyond to sleep with an inlined SetSleeping. The activity sweep partner_focus splits does not own these
// NPCs: found live by a write watch on a woken enemy, 24 ms after the wake the inlined sleep at 0x141bd43b1 ran.
//
// Same fix as partner_focus: each group's members are split by the nearer of the two foci (the player, the partner's body)
// and the engine's own update runs once per part with that focus, so a member is judged once, against its nearest focus.
#include "ds2/sneaking_focus.h"

#include <windows.h>

#include <algorithm>
#include <vector>

#include "decima/safe_read.h"
#include "decima/world_transform.h"
#include "ds2/engine.h"
#include "ds2/partner_focus.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kUpdateBody = 0x141bd3d50;  // (manager*): the activity update of the NPC managers
constexpr uintptr_t kEntityManagerGlobal = 0x14623DEB0;
constexpr uintptr_t kFocusOffset = 0xA0;  // WorldPosition: the point the update measures from

// Manager fields: an array of group records {int32 count, pad, Item** items} of 16 bytes, and its count.
constexpr uintptr_t kGroupCount = 0x138;  // int32
constexpr uintptr_t kGroups = 0x140;      // Group*
constexpr uintptr_t kItemEntity = 0x88;   // Item +0x88: the member's Entity*

struct Group {
    int32_t count;
    int32_t pad;
    uintptr_t* items;
};
static_assert(sizeof(Group) == 0x10);

using UpdateFn = void (*)(uintptr_t manager);
UpdateFn g_original = nullptr;

// Reused every call; the update runs on one thread.
std::vector<std::vector<uintptr_t>> g_nearThis;
std::vector<std::vector<uintptr_t>> g_nearPartner;

// Splits every group's members by the nearer focus. Members without a readable position stay with this machine's player.
void splitGroups(const Group* groups, int count, const decima::WorldPosition& focus, const decima::WorldPosition& partner) {
    g_nearThis.assign(count, {});
    g_nearPartner.assign(count, {});
    for (int g = 0; g < count; ++g) {
        for (int i = 0; i < groups[g].count; ++i) {
            const uintptr_t item = groups[g].items[i];
            decima::WorldPosition at;
            uintptr_t entity = 0;
            const bool nearPartner = item && decima::safeRead(item + kItemEntity, entity) && entity &&
                                     decima::safeRead(entity + ds2::kEntityTransform, at) &&
                                     partner_focus::squaredDistance(at, partner) < partner_focus::squaredDistance(at, focus);
            (nearPartner ? g_nearPartner : g_nearThis)[g].push_back(item);
        }
    }
}

// Points each group at its part and runs the engine's update.
void runOnParts(uintptr_t manager, Group* groups, int count, std::vector<std::vector<uintptr_t>>& parts) {
    for (int g = 0; g < count; ++g) {
        groups[g].count = static_cast<int32_t>(parts[g].size());
        groups[g].items = parts[g].data();
    }
    g_original(manager);
}

bool anyNearPartner() {
    return std::any_of(g_nearPartner.begin(), g_nearPartner.end(), [](const auto& group) { return !group.empty(); });
}

void updateBothFoci(uintptr_t manager, const decima::WorldPosition& partner) {
    Group* groups = ds2::field<Group*>(manager, kGroups);
    const int count = ds2::field<int32_t>(manager, kGroupCount);
    const uintptr_t entityManager = decima::readPointer(ds2::at(kEntityManagerGlobal));
    auto& focus = ds2::field<decima::WorldPosition>(entityManager, kFocusOffset);
    const decima::WorldPosition savedFocus = focus;
    const std::vector<Group> savedGroups(groups, groups + count);

    splitGroups(groups, count, savedFocus, partner);
    if (!anyNearPartner()) {
        g_original(manager);
        return;
    }
    static bool threadLogged = false;
    if (!threadLogged) {
        threadLogged = true;
        logger::write("sneaking_focus: the update with the partner's focus runs on thread %lu", GetCurrentThreadId());
    }
    runOnParts(manager, groups, count, g_nearThis);
    focus = partner;
    runOnParts(manager, groups, count, g_nearPartner);

    focus = savedFocus;
    std::copy(savedGroups.begin(), savedGroups.end(), groups);
}

void updateDetour(uintptr_t manager) {
    decima::WorldPosition partner;
    if (!partner_focus::partnerPosition(partner) || !decima::readPointer(ds2::at(kEntityManagerGlobal))) {
        g_original(manager);
        return;
    }
    updateBothFoci(manager, partner);
}

}  // namespace

namespace sneaking_focus {

void installEarly() {
    hooks::install("npc manager activity update", ds2::at(kUpdateBody), reinterpret_cast<void*>(&updateDetour),
                   reinterpret_cast<void**>(&g_original));
}

}  // namespace sneaking_focus
