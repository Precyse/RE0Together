// DEATH STRANDING 2: diagnostics for the NPC managers' activity update (adapter.ini diagnostics=1, never on in a shipped
// config): every 5 s each group's members, how many sleep and the numbers that decide it, and the members that stand near the
// partner (near the player when there is no partner) with their rank in the group's sorted list against its budget.
#include "ds2/sneaking_diag.h"

#include <windows.h>

#include <cmath>

#include "decima/world_transform.h"
#include "ds2/engine.h"
#include "ds2/entity_wake.h"
#include "ds2/partner_focus.h"
#include "ds2/sneaking_focus.h"
#include "ds2/sim_tick.h"
#include "ds2/sneaking_groups.h"
#include "log.h"

namespace {

using sneaking_groups::Group;
using sneaking_groups::memberEntity;

constexpr ULONGLONG kLogEveryMs = 5000;
constexpr double kSightMetres = 80.0;  // members this close to the watched point get a line of their own
constexpr int kMaxMemberLines = 8;

// How many members of the group are nearer (to the nearer focus) than this one: its place in the update's sorted list.
int rankInGroup(const Group& group, uintptr_t entity) {
    const float mine = sneaking_focus::nearestSquaredDistance(entity);
    int rank = 0;
    for (int i = 0; i < group.count; ++i) {
        const uintptr_t other = memberEntity(group.items[i]);
        if (other && other != entity && sneaking_focus::nearestSquaredDistance(other) < mine) ++rank;
    }
    return rank;
}

void logSightedMembers(int index, const Group& group, uint32_t budget, int& lines) {
    decima::WorldPosition partner;
    const char* label = "";
    if (!sneaking_focus::watchedPoint(partner, label)) return;
    for (int i = 0; i < group.count && lines < kMaxMemberLines; ++i) {
        const uintptr_t entity = memberEntity(group.items[i]);
        decima::WorldPosition at;
        if (!entity || !decima::safeRead(entity + ds2::kEntityTransform, at)) continue;
        const double metres = std::sqrt(partner_focus::squaredDistance(at, partner));
        if (metres > kSightMetres) continue;
        ++lines;
        logger::write("sneaking_diag: group %d member %p, %.0f m from %s, %s, rank %d of %d against budget %u", index,
                      reinterpret_cast<void*>(entity), metres, label, ds2::entityAsleep(entity) ? "asleep" : "awake",
                      rankInGroup(group, entity), group.count, budget);
    }
}

int sleepers(const Group& group) {
    int asleep = 0;
    for (int i = 0; i < group.count; ++i) {
        const uintptr_t entity = memberEntity(group.items[i]);
        if (entity && ds2::entityAsleep(entity)) ++asleep;
    }
    return asleep;
}

}  // namespace

namespace sneaking_diag {

void logGroups(uintptr_t manager) {
    if (!sim_tick::inWorld()) return;  // a load frees the members the groups hold
    static ULONGLONG nextLog = 0;
    const ULONGLONG now = GetTickCount64();
    if (now < nextLog) return;
    nextLog = now + kLogEveryMs;
    const auto groups = ds2::field<const Group*>(manager, sneaking_groups::kGroups);
    const int count = ds2::field<int32_t>(manager, sneaking_groups::kGroupCount);
    const auto nearRadius = ds2::field<const float*>(manager, sneaking_groups::kNear);
    const auto farRadius = ds2::field<const float*>(manager, sneaking_groups::kFar);
    const auto limit = ds2::field<const int32_t*>(manager, sneaking_groups::kLimit);
    const auto budget = ds2::field<const uint32_t*>(manager, sneaking_groups::kBudgets);
    int memberLines = 0;
    for (int g = 0; g < count; ++g) {
        logger::write("sneaking_diag: manager %p group %d: %d members, %d asleep, near %.0f far %.0f, limit %d, budget %u",
                      reinterpret_cast<void*>(manager), g, groups[g].count, sleepers(groups[g]), nearRadius[g], farRadius[g],
                      limit[g], budget[g]);
        logSightedMembers(g, groups[g], budget[g], memberLines);
    }
}

}  // namespace sneaking_diag
