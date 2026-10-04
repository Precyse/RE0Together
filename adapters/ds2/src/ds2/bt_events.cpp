// DEATH STRANDING 2: BT regions and catcher activations (docs/DS2_NOTES.md, "BT and catcher sync").
// Regions: DSWeatherManager keeps one flag byte per BT region (+0x72D0 + 16 * region, set by SetBtActiveRegion
// 0x141f09920, rebuilt each update from the fact references for fact-driven regions). The host reports the set; a guest
// vetoes its own SetBtActiveRegion and writes the host's set through the game's own setter on the simulation thread.
// Catchers: DSCatcherManager (global 0x14623F020) activates a territory locator through 0x1418e1300 (reached from the
// UUID script call 0x1418e1610 and two native callers) and a locator's tar through 0x141a0b980 (also called by the
// territory activation itself). The host reports each by the locator's UUID; a guest vetoes its own activations and
// replays the host's through the same two functions with the applying flag set.
#include "ds2/bt_events.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/sim_tick.h"
#include "hooks.h"
#include "log.h"
#include "reject_counters.h"
#include "remote_apply.h"

namespace {

constexpr uintptr_t kWeatherManagerGlobal = 0x14623fa10;
constexpr uintptr_t kBtRegionFlags = 0x72d0;  // + 16 * region: a byte, non-zero = BT-active
constexpr uintptr_t kBtRegionStride = 16;
constexpr uintptr_t kSetBtActiveRegion = 0x141f09920;  // (u8 region, u8 active)

constexpr uintptr_t kCatcherManagerGlobal = 0x14623F020;
constexpr uintptr_t kManagerLocatorCount = 0x10;  // i32
constexpr uintptr_t kManagerLocatorData = 0x18;   // pointer to locator pointers
constexpr uintptr_t kLocatorUuid = 0x10;
constexpr uintptr_t kLocatorTarActive = 0x1a8;  // byte: the tar already runs
constexpr uintptr_t kLocatorTarSpawn = 0xe0;    // pointer: what the tar needs to start; none = it does nothing
constexpr uintptr_t kTerritoryActivate = 0x1418e1300;  // (manager, locator, bool)
constexpr uintptr_t kTarActivate = 0x141a0b980;        // (locator)

constexpr size_t kMaxQueued = 64;
constexpr ULONGLONG kLocatorWaitMs = 10'000;  // a locator that is not loaded here by then is not coming (PendingQueue::kHoldUs)

using SetRegionFn = void (*)(uint8_t region, uint8_t active);
using TerritoryFn = void (*)(uintptr_t manager, uintptr_t locator, bool flag);
using TarFn = void (*)(uintptr_t locator);

SetRegionFn g_setRegion = nullptr;
TerritoryFn g_territory = nullptr;
TarFn g_tar = nullptr;

std::atomic<bool> g_host{false};
std::atomic<bool> g_guest{false};
thread_local int t_inTerritory = 0;  // inside a territory activation: its own tar call is part of that event

struct Waiting {
    bt_wire::CatcherEvent event;
    ULONGLONG deadline;
};

std::mutex g_mutex;
std::vector<bt_wire::CatcherEvent> g_outgoing;  // host: to send
std::vector<Waiting> g_incoming;                // guest: to replay
bool g_following = false;
uint64_t g_wanted = 0;

uintptr_t weatherManager() { return decima::readPointer(ds2::at(kWeatherManagerGlobal)); }

uint64_t readMask(uintptr_t manager) {
    uint8_t flags[bt_wire::kRegionCount] = {};
    for (int region = 0; region < bt_wire::kRegionCount; ++region) {
        decima::safeRead(manager + kBtRegionFlags + region * kBtRegionStride, flags[region]);
    }
    return bt_wire::maskOf(flags);
}

// The game's own change to the BT regions: the host lets it through, a guest keeps the host's set instead.
void setRegionDetour(uint8_t region, uint8_t active) {
    if (g_guest.load()) {
        static bool logged = false;
        if (!logged) logger::write("bt: a guest's own SetBtActiveRegion is vetoed (the host's set is followed)");
        logged = true;
        return;
    }
    g_setRegion(region, active);
}

bool readLocatorUuid(uintptr_t locator, bt_wire::CatcherEvent& event) {
    return decima::safeCopy(event.locator, locator + kLocatorUuid, sizeof(event.locator));
}

void report(bt_wire::CatcherKind kind, uint8_t flags, uintptr_t locator) {
    bt_wire::CatcherEvent event{};
    event.kind = static_cast<uint8_t>(kind);
    event.flags = flags;
    if (!readLocatorUuid(locator, event)) return;
    logger::write("bt: host catcher activation kind %u", event.kind);
    std::lock_guard lock(g_mutex);
    if (g_outgoing.size() < kMaxQueued) g_outgoing.push_back(event);
}

bool vetoedOnGuest(const char* what) {
    if (!g_guest.load() || remote_apply::active()) return false;
    logger::write("bt: a guest's own %s is vetoed (the host's activation is replayed)", what);
    return true;
}

void territoryDetour(uintptr_t manager, uintptr_t locator, bool flag) {
    if (vetoedOnGuest("territory activation")) return;
    ++t_inTerritory;
    g_territory(manager, locator, flag);
    --t_inTerritory;
    if (g_host.load() && !remote_apply::active()) {
        report(bt_wire::CatcherKind::Territory, flag ? bt_wire::kFlagTerritoryBool : 0, locator);
    }
}

bool tarWouldStart(uintptr_t locator) {
    uint8_t running = 1;
    return decima::safeRead(locator + kLocatorTarActive, running) && running == 0 &&
           decima::readPointer(locator + kLocatorTarSpawn) != 0;
}

void tarDetour(uintptr_t locator) {
    if (vetoedOnGuest("tar activation")) return;
    const bool reports = g_host.load() && !remote_apply::active() && t_inTerritory == 0 && tarWouldStart(locator);
    g_tar(locator);
    if (reports) report(bt_wire::CatcherKind::Tar, 0, locator);
}

// The locator of this world with that UUID, 0 while it is not loaded.
uintptr_t findLocator(uintptr_t manager, const uint8_t (&uuid)[bt_wire::kUuidSize]) {
    int32_t count = 0;
    const uintptr_t data = decima::readPointer(manager + kManagerLocatorData);
    if (!data || !decima::safeRead(manager + kManagerLocatorCount, count)) return 0;
    for (int32_t i = 0; i < count; ++i) {
        const uintptr_t locator = decima::readPointer(data + i * sizeof(uintptr_t));
        uint8_t found[bt_wire::kUuidSize];
        if (locator && decima::safeCopy(found, locator + kLocatorUuid, sizeof(found)) &&
            std::memcmp(found, uuid, sizeof(found)) == 0) {
            return locator;
        }
    }
    return 0;
}

// True once replayed; false while the locator is not loaded here.
bool replay(const bt_wire::CatcherEvent& event) {
    const uintptr_t manager = decima::readPointer(ds2::at(kCatcherManagerGlobal));
    const uintptr_t locator = manager ? findLocator(manager, event.locator) : 0;
    if (!locator) return false;
    remote_apply::Scope applying;
    if (static_cast<bt_wire::CatcherKind>(event.kind) == bt_wire::CatcherKind::Territory) {
        g_territory(manager, locator, (event.flags & bt_wire::kFlagTerritoryBool) != 0);
    } else {
        g_tar(locator);
    }
    logger::write("bt: replayed the host's catcher activation kind %u", event.kind);
    return true;
}

// Guest, simulation thread, ahead of the engine's object update: the host's BT regions, level-triggered.
void applyRegions() {
    uint64_t wanted;
    {
        std::lock_guard lock(g_mutex);
        if (!g_following) return;
        wanted = g_wanted;
    }
    const uintptr_t manager = weatherManager();
    if (!manager) return;
    const uint64_t changed = bt_wire::regionsToChange(readMask(manager), wanted);
    for (int region = 0; changed && region < bt_wire::kRegionCount; ++region) {
        if (bt_wire::isActive(changed, region)) g_setRegion(static_cast<uint8_t>(region), bt_wire::isActive(wanted, region));
    }
    static uint64_t logged = ~uint64_t{0};  // the engine may undo a flag and it is set again: logged once per host set
    if (changed && wanted != logged) {
        logged = wanted;
        logger::write("bt: followed the host's regions %016llx (flags now %016llx)", static_cast<unsigned long long>(wanted),
                      static_cast<unsigned long long>(readMask(manager)));
    }
}

// Guest, simulation thread: the host's activations whose locator is loaded here; the rest wait, then expire.
void applyCatcherEvents() {
    std::vector<Waiting> pending;
    {
        std::lock_guard lock(g_mutex);
        if (g_incoming.empty()) return;
        pending.swap(g_incoming);
    }
    const ULONGLONG now = GetTickCount64();
    std::vector<Waiting> keep;
    for (const Waiting& waiting : pending) {
        if (replay(waiting.event)) continue;
        if (now < waiting.deadline) {
            keep.push_back(waiting);
        } else {
            reject_counters::count(bt_wire::kMsgCatcherEvent, reject_counters::Reason::Expired);
        }
    }
    if (keep.empty()) return;
    std::lock_guard lock(g_mutex);
    g_incoming.insert(g_incoming.begin(), keep.begin(), keep.end());
}

}  // namespace

namespace bt_events {

void installEarly() {
    hooks::install("bt region setter", ds2::at(kSetBtActiveRegion), reinterpret_cast<void*>(&setRegionDetour),
                   reinterpret_cast<void**>(&g_setRegion));
    hooks::install("catcher territory activation", ds2::at(kTerritoryActivate), reinterpret_cast<void*>(&territoryDetour),
                   reinterpret_cast<void**>(&g_territory));
    hooks::install("catcher tar activation", ds2::at(kTarActivate), reinterpret_cast<void*>(&tarDetour),
                   reinterpret_cast<void**>(&g_tar));
    sim_tick::add(&applyRegions);
    sim_tick::add(&applyCatcherEvents);
}

void setRole(bool host, bool guest) {
    g_host = host;
    g_guest = guest;
    std::lock_guard lock(g_mutex);
    if (!host) g_outgoing.clear();
    if (!guest) {
        g_following = false;
        g_incoming.clear();
    }
}

bool readActiveRegions(bt_wire::BtEnv& out) {
    const uintptr_t manager = weatherManager();
    if (!manager) return false;
    out.activeRegions = readMask(manager);
    return true;
}

std::vector<bt_wire::CatcherEvent> takeCatcherEvents() {
    std::lock_guard lock(g_mutex);
    std::vector<bt_wire::CatcherEvent> out;
    out.swap(g_outgoing);
    return out;
}

void followRegions(const bt_wire::BtEnv& env) {
    std::lock_guard lock(g_mutex);
    g_wanted = env.activeRegions;
    g_following = true;
}

void replayCatcher(const bt_wire::CatcherEvent& event) {
    std::lock_guard lock(g_mutex);
    if (g_incoming.size() < kMaxQueued) g_incoming.push_back({event, GetTickCount64() + kLocatorWaitMs});
}

}  // namespace bt_events
