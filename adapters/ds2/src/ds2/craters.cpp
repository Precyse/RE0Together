// DEATH STRANDING 2: voidout craters (docs/DS2_NOTES.md, "Voidout craters").
// A crater is a 0x78-byte record in the DSEnvironmentManager (global 0x14623FA08): a committed array and a pending array
// (the ones added this frame), each with its own lock. Its key is the UUID of the catcher's crater locator. The only add is
// AddCrater 0x141ebc460 (reached from the catcher's annihilation anim event 0x141a05080 through 0x1418df6a0), the only
// removal RemoveCrater 0x1418dfda0. The host polls its set on the simulation tick; a guest adds what the host has and
// removes what it lacks through the same two calls, and its own annihilation event is vetoed so that one machine writes.
#include "ds2/craters.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>

#include "decima/safe_read.h"
#include "ds2/bt_diag.h"
#include "ds2/engine.h"
#include "ds2/sim_tick.h"
#include "hooks.h"
#include "log.h"
#include "remote_apply.h"

namespace {

using crater_wire::Crater;

constexpr uintptr_t kManagerGlobal = 0x14623FA08;
constexpr uintptr_t kPendingLock = 0x12408;  // CRITICAL_SECTION
constexpr uintptr_t kPendingCount = 0x12448;
constexpr uintptr_t kPendingEntries = 0x12450;  // pointer to the array
constexpr uintptr_t kCommittedLock = 0x12430;   // SRWLOCK
constexpr uintptr_t kCommittedCount = 0x12438;
constexpr uintptr_t kCommittedEntries = 0x12440;  // pointer to the array

constexpr size_t kEntrySize = 0x78;
constexpr uintptr_t kEntryPosition = 0x20;  // 3 doubles
constexpr uintptr_t kEntryRadius = 0x38;
constexpr uintptr_t kEntryArea = 0x58;  // u16
constexpr uintptr_t kEntryFlags = 0x60;
constexpr uintptr_t kEntryUuid = 0x64;
constexpr uint32_t kFlagRemoving = 0x2;
constexpr uint32_t kFlagFinished = 0x8;
constexpr uint32_t kKindShift = 4;
constexpr uint32_t kKindMask = 0xF;

constexpr uintptr_t kAddCrater = 0x141ebc460;     // (unused, const double pos[3], float radius, float countdown, unused, const GGUUID*, u32 kind)
constexpr uintptr_t kRemoveCrater = 0x1418dfda0;  // (const GGUUID*)
constexpr uintptr_t kAnnihilate = 0x141a05080;    // (DSCatcherAnnihilationComponent*)
constexpr uintptr_t kCraterCountdown = 0x143461DE0;  // float, seconds before a catcher's crater appears

// The area AddCrater files a new crater under: the pause-flag object's area when it has one, else the area named by the
// game module's current area source.
constexpr uintptr_t kAreaObjectGlobal = 0x146266968;
constexpr uintptr_t kAreaObjectSet = 0xA8;   // byte
constexpr uintptr_t kAreaObjectArea = 0x52;  // u16
constexpr uintptr_t kGameModuleGlobal = 0x14623E338;
constexpr uintptr_t kAreaSource = 0x4B0;
constexpr uintptr_t kAreaFromSource = 0x1426f4aa0;  // (source) -> u16
constexpr uint16_t kNoArea = 0xFFFF;

constexpr ULONGLONG kPollMs = 500;

using AddFn = void (*)(uintptr_t, const double*, float, float, uintptr_t, const uint8_t*, uint32_t);
using RemoveFn = void (*)(const uint8_t*);
using AnnihilateFn = void (*)(uintptr_t);
using AreaFn = uint64_t (*)(uintptr_t);

AnnihilateFn g_annihilate = nullptr;

std::atomic<bool> g_host{false};
std::atomic<bool> g_guest{false};

std::mutex g_mutex;
std::vector<Crater> g_hostSet;  // host: the last poll
bool g_hostRead = false;
std::vector<Crater> g_wanted;  // guest: the host's set
bool g_following = false;
std::set<std::array<uint8_t, crater_wire::kUuidSize>> g_heldLogged;  // simulation thread: adds held for another area

uintptr_t manager() { return decima::readPointer(ds2::at(kManagerGlobal)); }

bool sameUuid(const Crater& a, const Crater& b) { return std::memcmp(a.uuid, b.uuid, crater_wire::kUuidSize) == 0; }

bool contains(const std::vector<Crater>& set, const Crater& crater) {
    return std::any_of(set.begin(), set.end(), [&](const Crater& other) { return sameUuid(other, crater); });
}

// Appends the live entries of one array. False when the count or a read is not plausible (the caller skips the tick).
bool readArray(uintptr_t array, int32_t count, std::vector<Crater>& out) {
    if (count < 0 || static_cast<size_t>(count) > crater_wire::kMaxCraters) return false;
    if (count == 0) return true;
    if (!array) return false;
    for (int32_t i = 0; i < count; ++i) {
        uint8_t raw[kEntrySize];
        if (!decima::safeCopy(raw, array + i * kEntrySize, sizeof(raw))) return false;
        uint32_t flags;
        std::memcpy(&flags, raw + kEntryFlags, sizeof(flags));
        if (flags & (kFlagRemoving | kFlagFinished)) continue;
        Crater crater{};
        std::memcpy(crater.uuid, raw + kEntryUuid, sizeof(crater.uuid));
        std::memcpy(crater.position, raw + kEntryPosition, sizeof(crater.position));
        std::memcpy(&crater.radius, raw + kEntryRadius, sizeof(crater.radius));
        std::memcpy(&crater.area, raw + kEntryArea, sizeof(crater.area));
        crater.kind = (flags >> kKindShift) & kKindMask;
        if (!contains(out, crater)) out.push_back(crater);
    }
    return true;
}

// The pending array is read before the committed one: the engine moves entries from pending to committed, so an entry in
// motion is seen at least once. The locks are only tried, never waited for: a busy one skips the tick.
bool readLocal(uintptr_t mgr, std::vector<Crater>& out) {
    out.clear();
    auto* pendingLock = reinterpret_cast<CRITICAL_SECTION*>(mgr + kPendingLock);
    auto* committedLock = reinterpret_cast<SRWLOCK*>(mgr + kCommittedLock);
    int32_t pendingCount = 0, committedCount = 0;
    bool ok = false;
    if (TryEnterCriticalSection(pendingLock)) {
        ok = decima::safeRead(mgr + kPendingCount, pendingCount) &&
             readArray(decima::readPointer(mgr + kPendingEntries), pendingCount, out);
        LeaveCriticalSection(pendingLock);
    }
    if (!ok || !TryAcquireSRWLockShared(committedLock)) return false;
    ok = decima::safeRead(mgr + kCommittedCount, committedCount) &&
         readArray(decima::readPointer(mgr + kCommittedEntries), committedCount, out);
    ReleaseSRWLockShared(committedLock);
    if (!ok || out.size() > crater_wire::kMaxCraters) return false;
    crater_wire::sortByUuid(out);
    return true;
}

// The area AddCrater would file a crater under right now; kNoArea when the engine call faults.
uint16_t currentArea() {
    const uintptr_t areaObject = decima::readPointer(ds2::at(kAreaObjectGlobal));
    uint8_t set = 0;
    uint16_t area = kNoArea;
    if (areaObject && decima::safeRead(areaObject + kAreaObjectSet, set) && set) {
        decima::safeRead(areaObject + kAreaObjectArea, area);
        return area;
    }
    const uintptr_t module = decima::readPointer(ds2::at(kGameModuleGlobal));
    const uintptr_t source = module ? decima::readPointer(module + kAreaSource) : 0;
    if (!source) return kNoArea;
    __try {
        return static_cast<uint16_t>(reinterpret_cast<AreaFn>(ds2::at(kAreaFromSource))(source));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return kNoArea;
    }
}

bool callAdd(const Crater& crater, float countdown) {
    __try {
        reinterpret_cast<AddFn>(ds2::at(kAddCrater))(0, crater.position, crater.radius, countdown, 0, crater.uuid, crater.kind);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool callRemove(const uint8_t (&uuid)[crater_wire::kUuidSize]) {
    __try {
        reinterpret_cast<RemoveFn>(ds2::at(kRemoveCrater))(uuid);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool addCrater(const Crater& crater) {
    float countdown = 0;
    if (!decima::safeRead(ds2::at(kCraterCountdown), countdown)) return false;
    remote_apply::Scope applying;
    return callAdd(crater, countdown);
}

// The catcher's annihilation anim event: the host lets it run; a guest's puppet must not add a crater or end the game.
void annihilateDetour(uintptr_t component) {
    if (g_guest.load() && !remote_apply::active()) {
        static bool logged = false;
        if (!logged) logger::write("craters: a guest's own catcher annihilation is vetoed (the host's crater is followed)");
        logged = true;
        return;
    }
    const bool probed = g_host.load();
    if (probed) bt_diag::annihilation(component, true);
    g_annihilate(component);
    if (probed) bt_diag::annihilation(component, false);
}

// Host, simulation thread.
void pollHost() {
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    if (!g_host.load() || now - last < kPollMs) return;
    last = now;
    const uintptr_t mgr = manager();
    std::vector<Crater> set;
    if (!mgr || !readLocal(mgr, set)) return;
    std::lock_guard lock(g_mutex);
    g_hostSet = std::move(set);
    g_hostRead = true;
}

// Guest, simulation thread: the host's set, level-triggered.
void followHost() {
    if (!g_guest.load() || !sim_tick::worldReady()) return;
    std::vector<Crater> wanted;
    {
        std::lock_guard lock(g_mutex);
        if (!g_following) return;
        wanted = g_wanted;
    }
    const uintptr_t mgr = manager();
    std::vector<Crater> local;
    if (!mgr || !readLocal(mgr, local)) return;
    const uint16_t area = currentArea();
    for (const Crater& crater : wanted) {
        if (contains(local, crater)) continue;
        if (crater.area != area) {
            std::array<uint8_t, crater_wire::kUuidSize> key;
            std::memcpy(key.data(), crater.uuid, key.size());
            if (g_heldLogged.insert(key).second) {
                logger::write("craters: held %s (host area %u, here %u)", craters::uuidText(crater.uuid).c_str(), crater.area, area);
            }
        } else if (addCrater(crater)) {
            logger::write("craters: added host crater %s", craters::uuidText(crater.uuid).c_str());
        }
    }
    for (const Crater& crater : local) {
        if (contains(wanted, crater)) continue;
        remote_apply::Scope applying;
        if (callRemove(crater.uuid)) logger::write("craters: removed %s", craters::uuidText(crater.uuid).c_str());
    }
}

}  // namespace

namespace craters {

std::string uuidText(const uint8_t* uuid) {
    char text[2 * crater_wire::kUuidSize + 1] = {};
    for (size_t i = 0; i < crater_wire::kUuidSize; ++i) sprintf_s(text + i * 2, 3, "%02x", uuid[i]);
    return text;
}

void installEarly() {
    hooks::install("catcher annihilation", ds2::at(kAnnihilate), reinterpret_cast<void*>(&annihilateDetour),
                   reinterpret_cast<void**>(&g_annihilate));
    sim_tick::add(&pollHost, "craters host", sim_tick::Gate::Gameplay);
    sim_tick::add(&followHost, "craters guest", sim_tick::Gate::Gameplay);
}

void setRole(bool host, bool guest) {
    g_host = host;
    g_guest = guest;
    std::lock_guard lock(g_mutex);
    if (!host) {
        g_hostSet.clear();
        g_hostRead = false;
    }
    if (!guest) {
        g_following = false;
        g_wanted.clear();
    }
}

bool hostSet(std::vector<Crater>& out) {
    std::lock_guard lock(g_mutex);
    if (!g_hostRead) return false;
    out = g_hostSet;
    return true;
}

void follow(std::vector<Crater> set) {
    std::lock_guard lock(g_mutex);
    g_wanted = std::move(set);
    g_following = true;
}

void logAll() {
    const uintptr_t mgr = manager();
    std::vector<Crater> set;
    if (!mgr || !readLocal(mgr, set)) {
        logger::write("craters: no readable set");
        return;
    }
    logger::write("craters: %zu entries (area here %u)", set.size(), currentArea());
    for (const Crater& crater : set) {
        logger::write("craters:   %s at %.1f %.1f %.1f radius %.1f area %u kind %u", uuidText(crater.uuid).c_str(),
                      crater.position[0], crater.position[1], crater.position[2], crater.radius, crater.area, crater.kind);
    }
}

bool add(const double (&position)[3], float radius, const uint8_t (&uuid)[crater_wire::kUuidSize]) {
    Crater crater{};
    std::memcpy(crater.uuid, uuid, sizeof(uuid));
    std::memcpy(crater.position, position, sizeof(position));
    crater.radius = radius;
    return manager() && addCrater(crater);
}

bool remove(const uint8_t (&uuid)[crater_wire::kUuidSize]) { return manager() && callRemove(uuid); }

}  // namespace craters
