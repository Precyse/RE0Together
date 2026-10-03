// DEATH STRANDING 2: the marker manager (global 0x14623E638) holds all map and HUD markers in one array of pointers,
// split into groups by marker type, and some types are meant to hold exactly one marker: the player marker (type 1,
// created by every DSPlayerEntity at init) and the backpack marker (type 25, created by every DSBackpackComponent).
// Each creation records the group as {start = its index, count = 1}, and removing the entity or component removes the
// group by start and count, not by pointer. With the remote body there are two of each: the group then names the
// remote's marker, Sam's sits outside every group, and when Sam's entity goes at the unload the wrong slot is removed
// and Sam's freed marker stays in the array for the HUD loop to call into. Here the remote's marker is taken out of the
// array and the group is pointed back at Sam's marker. The remote's entity keeps its pointer to its marker (it reads
// it every update); at the unload either removal then removes that one group and the other finds it empty.
#include "ds2/remote_marker.h"

#include <windows.h>

#include <array>
#include <cstring>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "log.h"

namespace {

constexpr uintptr_t kMarkerManagerGlobal = 0x14623E638;
constexpr uintptr_t kLock = 0x10;           // SRW lock
constexpr uintptr_t kMarkers = 0x20;        // array of marker pointers
constexpr uintptr_t kMarkerCount = 0x7520;  // i32
constexpr uintptr_t kGroupCounts = 0x7524;  // i32 per marker type
constexpr uintptr_t kGroupStarts = 0x759C;  // i32 per marker type (the tables are 30 entries, 0x78 bytes, apart)
constexpr int kMarkerTypes = 30;
constexpr int32_t kMaxMarkers = 1 << 14;

struct Single {
    bool present = false;
    int32_t start = 0;
    uintptr_t marker = 0;
};
std::array<Single, kMarkerTypes> g_singles;

uintptr_t managerOf() { return decima::readPointer(ds2::at(kMarkerManagerGlobal)); }
int32_t& count(uintptr_t manager) { return ds2::field<int32_t>(manager, kMarkerCount); }
int32_t& groupCount(uintptr_t manager, int type) { return ds2::field<int32_t>(manager, kGroupCounts + type * sizeof(int32_t)); }
int32_t& groupStart(uintptr_t manager, int type) { return ds2::field<int32_t>(manager, kGroupStarts + type * sizeof(int32_t)); }

uintptr_t& marker(uintptr_t manager, int32_t index) {
    return ds2::field<uintptr_t>(manager, kMarkers + static_cast<uintptr_t>(index) * sizeof(uintptr_t));
}

int32_t indexOf(uintptr_t manager, uintptr_t wanted) {
    for (int32_t i = 0; i < count(manager); ++i) {
        if (marker(manager, i) == wanted) return i;
    }
    return -1;
}

void removeAt(uintptr_t manager, int32_t at) {
    const int32_t total = count(manager);
    std::memmove(&marker(manager, at), &marker(manager, at + 1), static_cast<size_t>(total - at - 1) * sizeof(uintptr_t));
    count(manager) = total - 1;
    for (int type = 0; type < kMarkerTypes; ++type) {
        if (groupStart(manager, type) > at) --groupStart(manager, type);
    }
}

}  // namespace

namespace remote_marker {

void snapshot() {
    const uintptr_t manager = managerOf();
    if (!manager) return;
    for (int type = 0; type < kMarkerTypes; ++type) {
        g_singles[type] = {};
        const int32_t start = groupStart(manager, type);
        if (groupCount(manager, type) == 1 && start >= 0 && start < count(manager)) {
            g_singles[type] = {true, start, marker(manager, start)};
        }
    }
}

void repair() {
    const uintptr_t manager = managerOf();
    if (!manager) return;
    for (int type = 0; type < kMarkerTypes; ++type) {
        const Single& sam = g_singles[type];
        if (!sam.present) continue;
        const int32_t start = groupStart(manager, type);
        if (groupCount(manager, type) != 1 || start < 0 || start >= count(manager) || marker(manager, start) == sam.marker) continue;
        auto* lock = reinterpret_cast<PSRWLOCK>(manager + kLock);
        AcquireSRWLockExclusive(lock);
        const int32_t current = groupStart(manager, type);
        const int32_t samAt = indexOf(manager, sam.marker);
        if (groupCount(manager, type) == 1 && current >= 0 && current < count(manager) && samAt >= 0 &&
            marker(manager, current) != sam.marker && count(manager) < kMaxMarkers) {
            removeAt(manager, current);
            groupStart(manager, type) = indexOf(manager, sam.marker);
            logger::write("remote_marker: the group of marker type %d was taken over by the remote's marker (index %d): taken out, "
                          "the group points at Sam's marker again (index %d)", type, current, groupStart(manager, type));
        }
        ReleaseSRWLockExclusive(lock);
    }
}

}  // namespace remote_marker
