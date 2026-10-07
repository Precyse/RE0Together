// DEATH STRANDING 2: which entities are simulated is decided once per frame per entity category (EntityManagerGame +0x20E0,
// 58 tables of 0xE8 bytes, walked by 0x1401826A0 through 0x140171D90). A table holds every entity of its category and one
// focus position (+0x30, copied each frame from the streaming manager's single observer, 0x146266958 +0x20). The update
// measures each member's distance to that one point: members past the table's sleep radius (+0x28), or inside it but not
// fully streamed in, are put to sleep (Entity::SetSleeping 0x140131200); near ones that are streamed in wake. The partner's
// body is a player in PlayerManager but no focus: the engine reads no other player position, so enemies around the partner,
// far from this machine's player, were re-slept a few milliseconds after any wake.
//
// The fix keeps the engine's own decision and runs it once per focus: each table's members are split by the nearer of the
// two foci (this machine's player, the partner's body) and the original update runs on each part with its focus, so a
// member is judged exactly once, against the focus nearest to it. The awake budget (+0x18) applies per focus.
// Streaming itself still follows one observer (see tools/ds2/out/analysis/STREAMING.md).
#include "ds2/partner_focus.h"

#include <windows.h>

#include <vector>

#include "decima/safe_read.h"
#include "decima/world_transform.h"
#include "ds2/engine.h"
#include "ds2/remote_player.h"
#include "ds2/sim_tick.h"
#include "hooks.h"
#include "log.h"

namespace {

using partner_focus::partnerPosition;
using partner_focus::squaredDistance;

constexpr uintptr_t kCategoryUpdate = 0x140171d90;  // (category table*)

// Category table fields.
constexpr uintptr_t kTableAwakeCount = 0x1C;   // int32: members kept awake this frame (counted against the budget +0x18)
constexpr uintptr_t kTableFocus = 0x30;        // WorldPosition: the point the update measures from
constexpr uintptr_t kTableMemberCount = 0xD8;  // int32
constexpr uintptr_t kTableMembers = 0xE0;      // Entity*[count]

constexpr ULONGLONG kLogEveryMs = 5000;

using UpdateFn = void (*)(uintptr_t table);
UpdateFn g_original = nullptr;

// Reused every call; the update runs on one thread, one table at a time.
std::vector<uintptr_t> g_nearThis;
std::vector<uintptr_t> g_nearPartner;

// Entities handed to the partner focus: running total of the sweep in progress, and the last finished sweep.
uintptr_t g_lastTable = 0;
size_t g_sweepTotal = 0;
size_t g_lastSweepTotal = 0;
ULONGLONG g_nextLogAt = 0;

// Splits the table's members by the nearer focus. Members without a readable position stay with this machine's player.
void splitMembers(uintptr_t table, const decima::WorldPosition& partner) {
    g_nearThis.clear();
    g_nearPartner.clear();
    const int count = ds2::field<int32_t>(table, kTableMemberCount);
    const uintptr_t* members = ds2::field<const uintptr_t*>(table, kTableMembers);
    const decima::WorldPosition focus = ds2::field<decima::WorldPosition>(table, kTableFocus);
    for (int i = 0; i < count; ++i) {
        decima::WorldPosition at;
        const bool nearPartner = members[i] && decima::safeRead(members[i] + ds2::kEntityTransform, at) &&
                                 squaredDistance(at, partner) < squaredDistance(at, focus);
        (nearPartner ? g_nearPartner : g_nearThis).push_back(members[i]);
    }
}

void runOnMembers(uintptr_t table, std::vector<uintptr_t>& members) {
    ds2::field<int32_t>(table, kTableMemberCount) = static_cast<int32_t>(members.size());
    ds2::field<const uintptr_t*>(table, kTableMembers) = members.data();
    g_original(table);
}

// The original update twice, each on its own members and focus; the table is put back as it was.
void updateBothFoci(uintptr_t table, const decima::WorldPosition& partner) {
    const auto savedCount = ds2::field<int32_t>(table, kTableMemberCount);
    const auto savedMembers = ds2::field<const uintptr_t*>(table, kTableMembers);
    const auto savedFocus = ds2::field<decima::WorldPosition>(table, kTableFocus);

    runOnMembers(table, g_nearThis);
    const auto awakeNearThis = ds2::field<int32_t>(table, kTableAwakeCount);
    ds2::field<decima::WorldPosition>(table, kTableFocus) = partner;
    runOnMembers(table, g_nearPartner);
    ds2::field<int32_t>(table, kTableAwakeCount) += awakeNearThis;

    ds2::field<decima::WorldPosition>(table, kTableFocus) = savedFocus;
    ds2::field<const uintptr_t*>(table, kTableMembers) = savedMembers;
    ds2::field<int32_t>(table, kTableMemberCount) = savedCount;
}

// Tables are walked in ascending order each frame: a lower address starts a new sweep, and the first sweep after the log is
// due prints the finished one's total.
void countSweep(uintptr_t table, size_t handedToPartner) {
    if (table <= g_lastTable) {
        g_lastSweepTotal = g_sweepTotal;
        g_sweepTotal = 0;
        const ULONGLONG now = GetTickCount64();
        if (now >= g_nextLogAt) {
            g_nextLogAt = now + kLogEveryMs;
            logger::write("partner_focus: %zu entities simulated around the partner last frame", g_lastSweepTotal);
        }
    }
    g_lastTable = table;
    g_sweepTotal += handedToPartner;
}

void categoryUpdateDetour(uintptr_t table) {
    decima::WorldPosition partner;
    if (!sim_tick::inWorld() || !partnerPosition(partner)) {
        g_original(table);
        return;
    }
    splitMembers(table, partner);
    if (g_nearPartner.empty()) {
        g_original(table);
    } else {
        updateBothFoci(table, partner);
    }
    countSweep(table, g_nearPartner.size());
}

}  // namespace

namespace partner_focus {

bool partnerPosition(decima::WorldPosition& out) {
    const uintptr_t body = remote_player::entity();
    return body && remote_player::isLive() && decima::safeRead(body + ds2::kEntityTransform, out);
}

double squaredDistance(const decima::WorldPosition& a, const decima::WorldPosition& b) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

void installEarly() {
    hooks::install("entity activity update", ds2::at(kCategoryUpdate), reinterpret_cast<void*>(&categoryUpdateDetour),
                   reinterpret_cast<void**>(&g_original));
}

}  // namespace partner_focus
