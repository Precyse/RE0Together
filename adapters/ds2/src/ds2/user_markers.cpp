// DEATH STRANDING 2: the local player's user markers, read from the marker manager (global 0x14623E638). The manager creates one
// pool of 100 DSUserMarker objects (0x70 bytes each, creation 0x140c2dca0, stored at manager +0x7620) and never resizes it:
// placing a marker takes a free entry of its group's slice of 20 (AddUserMarker 0x140c0ad80), removing one clears the active
// bit (RemoveUserMarker 0x140c0afb0). Layout: +0x10 position (3 floats, world metres, Z up), +0x20 id, +0x30 bit 0 active,
// +0x60 group 0..4, +0x61 index 0..19, +0x62 sequence (0 = a point interpolated on a line, 1 or more = a placed point).
// Read-only: the partner's pings are drawn by the overlay, not written into the game.
#include "ds2/user_markers.h"

#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <cstring>
#include <mutex>

#include "decima/safe_read.h"
#include "ds2/checked_hook.h"
#include "ds2/engine.h"
#include "ds2/sim_tick.h"
#include "log.h"

namespace {

using ping_wire::Ping;

constexpr uintptr_t kPoolPointer = 0x7620;
constexpr size_t kPoolEntries = ping_wire::kMaxPings;
constexpr size_t kEntrySize = 0x70;
constexpr uintptr_t kEntryPosition = 0x10, kEntryId = 0x20, kEntryFlags = 0x30, kEntryGroup = 0x60, kEntryIndex = 0x61,
                    kEntrySequence = 0x62;
constexpr uint8_t kActiveBit = 1;
constexpr ULONGLONG kPollMs = 250;

constexpr uintptr_t kAddUserMarker = 0x140c0ad80;
constexpr const char* kAddUserMarkerStart = "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 41 57";

// (manager, position[4], tag, group, index, sequence (-1 = the previous placed point's + 1), height pending, removed on arrival) -> added
using AddFn = bool (*)(uintptr_t, const float*, uint8_t, uint32_t, uint32_t, int32_t, uint8_t, uint8_t);
AddFn g_add = nullptr;

std::mutex g_mutex;
std::vector<Ping> g_current;  // the last read, sorted by id
bool g_read = false;
bool g_changed = false;  // g_current differs from what takeChangedSet last handed out

// The placed points of the pool, sorted by id. False when the manager or the pool is missing or an entry holds values this
// code was not written for (a layout mismatch must send nothing): the caller skips the tick.
bool readPool(std::vector<Ping>& out) {
    const uintptr_t manager = decima::readPointer(ds2::at(ds2::kMarkerManagerGlobal));
    const uintptr_t pool = manager ? decima::readPointer(manager + kPoolPointer) : 0;
    static uint8_t raw[kPoolEntries * kEntrySize];  // the simulation thread is the only reader
    if (!pool || !decima::safeCopy(raw, pool, sizeof(raw))) return false;
    for (size_t i = 0; i < kPoolEntries; ++i) {
        const uint8_t* entry = raw + i * kEntrySize;
        if ((entry[kEntryFlags] & kActiveBit) == 0 || entry[kEntrySequence] == 0) continue;
        Ping ping{};
        std::memcpy(&ping.id, entry + kEntryId, sizeof(ping.id));
        std::memcpy(ping.position, entry + kEntryPosition, sizeof(ping.position));
        ping.group = entry[kEntryGroup];
        ping.index = entry[kEntryIndex];
        ping.sequence = entry[kEntrySequence];
        if (!ping_wire::plausible(ping)) return false;
        out.push_back(ping);
    }
    ping_wire::sortById(out);
    return std::adjacent_find(out.begin(), out.end(), [](const Ping& a, const Ping& b) { return a.id == b.id; }) == out.end();
}

void poll() {
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - last < kPollMs || !sim_tick::worldReady()) return;
    last = now;
    std::vector<Ping> set;
    if (!readPool(set)) return;
    std::lock_guard lock(g_mutex);
    g_read = true;
    if (ping_wire::sameSet(set, g_current)) return;
    g_current = std::move(set);
    g_changed = true;
}

bool addDetour(uintptr_t manager, const float* position, uint8_t tag, uint32_t group, uint32_t index, int32_t sequence,
               uint8_t heightPending, uint8_t arrivalRemoves) {
    float at[3] = {};
    decima::safeCopy(at, reinterpret_cast<uintptr_t>(position), sizeof(at));
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - ds2::at(ds2::kImageBase) + ds2::kImageBase;
    logger::write("user_markers: add from %llx thread %lu group %u index %u sequence %d tag %u height %u arrival %u at %.1f %.1f %.1f",
                  static_cast<unsigned long long>(caller), GetCurrentThreadId(), group, index, sequence, tag, heightPending,
                  arrivalRemoves, at[0], at[1], at[2]);
    return g_add(manager, position, tag, group, index, sequence, heightPending, arrivalRemoves);
}

}  // namespace

namespace user_markers {

void installEarly(bool diagnostics) {
    sim_tick::add(&poll, "user markers", sim_tick::Gate::Gameplay);
    if (diagnostics) {
        checked_hook::install("user marker add", kAddUserMarker, kAddUserMarkerStart, reinterpret_cast<void*>(&addDetour),
                              reinterpret_cast<void**>(&g_add));
    }
}

bool takeChangedSet(std::vector<Ping>& out) {
    std::lock_guard lock(g_mutex);
    if (!g_changed) return false;
    g_changed = false;
    out = g_current;
    return true;
}

bool currentSet(std::vector<Ping>& out) {
    std::lock_guard lock(g_mutex);
    out = g_current;
    return g_read;
}

}  // namespace user_markers
