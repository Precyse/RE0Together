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

#include <atomic>
#include <vector>

#include "decima/safe_read.h"
#include "decima/world_transform.h"
#include "ds2/engine.h"
#include "ds2/enemy_host.h"
#include "ds2/entity_wake.h"
#include "ds2/health_watch.h"
#include "ds2/remote_player.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kCategoryUpdate = 0x140171d90;  // (category table*)

// Category table fields.
constexpr uintptr_t kTableAwakeCount = 0x1C;   // int32: members kept awake this frame (counted against the budget +0x18)
constexpr uintptr_t kTableFocus = 0x30;        // WorldPosition: the point the update measures from
constexpr uintptr_t kTableMemberCount = 0xD8;  // int32
constexpr uintptr_t kTableMembers = 0xE0;      // Entity*[count]

// Fields the log prints for a table that still has sleeping members near the partner after the update.
constexpr uintptr_t kTableBudget = 0x18;      // int32: awake budget
constexpr uintptr_t kTableNearRadius = 0x24;  // float
constexpr uintptr_t kTableFarRadius = 0x28;   // float
constexpr uintptr_t kTableArea = 0x48;        // optional area object: members outside it sleep
constexpr uintptr_t kEntityManagerGlobal = 0x14623DEB0;
constexpr uintptr_t kTables = 0x20E0;
constexpr uintptr_t kTableSize = 0xE8;
constexpr uintptr_t kEntityFlags = 0x98;  // the activity flags the sleep decision writes
constexpr uintptr_t kStreamingManagerGlobal = 0x146266958;  // StreamingStrategyManagerGame*
constexpr uintptr_t kStreamTest = 0x1426E64F0;              // (manager, position*, radius) -> 2 when streamed in
constexpr float kNearStreamRadius = 4.0f, kMidStreamRadius = 2.0f;  // the radii the update passes (constants in the code)

constexpr ULONGLONG kLogEveryMs = 5000;

constexpr uint8_t kStreamedIn = 2;

using UpdateFn = void (*)(uintptr_t table);
using StreamTestFn = uint8_t (*)(uintptr_t manager, uintptr_t position, float radius);
UpdateFn g_original = nullptr;
StreamTestFn g_streamTest = nullptr;
std::atomic<bool> g_streamedTestOverride{false};  // test toggle: members near the partner count as streamed in
bool g_judgingPartnerPart = false;                 // the original update is running on the partner's members

// Reused every call; the update runs on one thread, one table at a time.
std::vector<uintptr_t> g_nearThis;
std::vector<uintptr_t> g_nearPartner;

// Entities handed to the partner focus: running total of the sweep in progress, and the last finished sweep.
uintptr_t g_lastTable = 0;
size_t g_sweepTotal = 0;
size_t g_lastSweepTotal = 0;
ULONGLONG g_nextLogAt = 0;
bool g_diagnostics = false;
bool g_logSweep = false;  // this sweep prints its tables' sleepers

bool partnerPosition(decima::WorldPosition& out) {
    const uintptr_t body = remote_player::entity();
    return body && remote_player::isLive() && decima::safeRead(body + ds2::kEntityTransform, out);
}

double squaredDistance(const decima::WorldPosition& a, const decima::WorldPosition& b) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

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

// The update asks the streaming manager whether the tile data at a member is loaded; it follows this machine's one
// observer, so around a far partner the answer is "not streamed in" and the member would sleep. With the test toggle on,
// the partner's members are judged as streamed in.
uint8_t streamTestDetour(uintptr_t manager, uintptr_t position, float radius) {
    const uint8_t result = g_streamTest(manager, position, radius);
    return g_judgingPartnerPart && g_streamedTestOverride.load() ? kStreamedIn : result;
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
    g_judgingPartnerPart = true;
    runOnMembers(table, g_nearPartner);
    g_judgingPartnerPart = false;
    ds2::field<int32_t>(table, kTableAwakeCount) += awakeNearThis;

    ds2::field<decima::WorldPosition>(table, kTableFocus) = savedFocus;
    ds2::field<const uintptr_t*>(table, kTableMembers) = savedMembers;
    ds2::field<int32_t>(table, kTableMemberCount) = savedCount;
}

// Tables are walked in ascending order each frame: a lower address starts a new sweep, and the sweep after the log is due
// prints the finished one's total and each table's sleepers.
void countSweep(uintptr_t table, size_t handedToPartner) {
    if (table <= g_lastTable) {
        g_lastSweepTotal = g_sweepTotal;
        g_sweepTotal = 0;
        const ULONGLONG now = GetTickCount64();
        g_logSweep = now >= g_nextLogAt;
        if (g_logSweep) {
            g_nextLogAt = now + kLogEveryMs;
            logger::write("partner_focus: %zu entities simulated around the partner last frame", g_lastSweepTotal);
        }
    }
    g_lastTable = table;
    g_sweepTotal += handedToPartner;
}

// Once: wakes a sleeping enemy near the partner and watches its flags, so the log names the code that puts it back to sleep
// (the write watch logs each distinct writing instruction with its stack).
void watchEnemySleeper() {
    static bool watched = false;
    if (watched) return;
    uintptr_t sleeper = 0;
    for (const uintptr_t member : g_nearPartner) {
        if (member && ds2::entityAsleep(member) && enemy_host::tracks(member)) sleeper = member;
    }
    if (!sleeper) return;
    watched = true;
    health_watch::arm(sleeper + kEntityFlags);
    ds2::wakeEntity(sleeper);
    logger::write("partner_focus: watching the flags of sleeper %p after waking it", reinterpret_cast<void*>(sleeper));
}

// Names why members near the partner are still asleep: the table's budget, radii and area decide it.
void logSleepers(uintptr_t table) {
    size_t asleep = 0;
    uintptr_t sample = 0;
    for (const uintptr_t member : g_nearPartner) {
        if (!member || !ds2::entityAsleep(member)) continue;
        ++asleep;
        sample = member;
    }
    if (asleep == 0) return;
    const uintptr_t manager = decima::readPointer(ds2::at(kStreamingManagerGlobal));
    const auto streamTest = reinterpret_cast<uint8_t (*)(uintptr_t, uintptr_t, float)>(ds2::at(kStreamTest));
    logger::write("partner_focus: a sleeper's stream test: near radius %d, mid radius %d (2 = streamed in)",
                  streamTest(manager, sample + ds2::kEntityTransform, kNearStreamRadius),
                  streamTest(manager, sample + ds2::kEntityTransform, kMidStreamRadius));
    watchEnemySleeper();
    const uintptr_t first = decima::readPointer(ds2::at(kEntityManagerGlobal)) + kTables;
    logger::write("partner_focus: table %llu has %zu of %zu members near the partner asleep (budget %d, awake %d, near %.0f, far %.0f, area %p)",
                  static_cast<unsigned long long>((table - first) / kTableSize), asleep, g_nearPartner.size(),
                  ds2::field<int32_t>(table, kTableBudget), ds2::field<int32_t>(table, kTableAwakeCount),
                  ds2::field<float>(table, kTableNearRadius), ds2::field<float>(table, kTableFarRadius),
                  reinterpret_cast<void*>(ds2::field<uintptr_t>(table, kTableArea)));
}

void categoryUpdateDetour(uintptr_t table) {
    decima::WorldPosition partner;
    if (!partnerPosition(partner)) {
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
    if (g_diagnostics && g_logSweep) logSleepers(table);
}

}  // namespace

namespace partner_focus {

void installEarly(bool diagnostics) {
    g_diagnostics = diagnostics;
    hooks::install("streaming stream test", ds2::at(kStreamTest), reinterpret_cast<void*>(&streamTestDetour),
                   reinterpret_cast<void**>(&g_streamTest));
    hooks::install("entity activity update", ds2::at(kCategoryUpdate), reinterpret_cast<void*>(&categoryUpdateDetour),
                   reinterpret_cast<void**>(&g_original));
}

void setStreamedInOverride(bool on) {
    g_streamedTestOverride = on;
    logger::write("partner_focus: members near the partner %s judged as streamed in", on ? "are" : "are no longer");
}

}  // namespace partner_focus
