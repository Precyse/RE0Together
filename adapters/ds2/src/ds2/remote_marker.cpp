// DEATH STRANDING 2: the marker manager (global 0x14623E638) holds all map and HUD markers in one array of pointers,
// split into groups by marker type, and it expects exactly one player marker (type 1). Every DSPlayerEntity creates one
// when it is initialised and records the group as {start = its index, count = 1}; removing a player entity removes
// the group by start and count, not by pointer. With the remote body there are two player entities: the group then
// names the remote's marker, Sam's sits outside every group, and when Sam's entity goes at the unload the wrong slot
// is removed and Sam's freed marker stays in the array for the HUD loop to call into. Here the remote's marker is
// taken out of the array (its entity keeps the pointer: it reads the marker every update) and the group is pointed
// back at Sam's marker. At the unload either entity's removal then removes that one group, and the other finds it empty.
#include "ds2/remote_marker.h"

#include <windows.h>

#include <cstring>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/remote_player.h"
#include "log.h"

namespace {

constexpr uintptr_t kMarkerManagerGlobal = 0x14623E638;
constexpr uintptr_t kLock = 0x10;              // SRW lock
constexpr uintptr_t kMarkers = 0x20;           // array of marker pointers
constexpr uintptr_t kMarkerCount = 0x7520;     // i32
constexpr uintptr_t kGroupCounts = 0x7524;     // i32 per marker type
constexpr uintptr_t kGroupStarts = 0x759C;     // i32 per marker type
constexpr int kMarkerTypes = 30;               // the start table (0x759C) follows the count table (0x7524) 0x78 bytes later
constexpr int kPlayerType = 1;
constexpr uintptr_t kEntityPlayerMarker = 0x5668;
constexpr int32_t kMaxMarkers = 1 << 14;

int32_t& count(uintptr_t manager) { return ds2::field<int32_t>(manager, kMarkerCount); }

uintptr_t& marker(uintptr_t manager, int32_t index) {
    return ds2::field<uintptr_t>(manager, kMarkers + static_cast<uintptr_t>(index) * sizeof(uintptr_t));
}

int32_t indexOf(uintptr_t manager, uintptr_t wanted) {
    for (int32_t i = 0; i < count(manager); ++i) {
        if (marker(manager, i) == wanted) return i;
    }
    return -1;
}

}  // namespace

namespace remote_marker {

void detachRemote() {
    const uintptr_t manager = decima::readPointer(ds2::at(kMarkerManagerGlobal));
    const uintptr_t remote = remote_player::entity();
    const uintptr_t sam = remote_player::samEntity();
    if (!manager || !remote || !sam) return;
    const uintptr_t remoteMarker = decima::readPointer(remote + kEntityPlayerMarker);
    const uintptr_t samMarker = decima::readPointer(sam + kEntityPlayerMarker);
    if (!remoteMarker || !samMarker || remoteMarker == samMarker) return;
    auto* lock = reinterpret_cast<PSRWLOCK>(manager + kLock);
    AcquireSRWLockExclusive(lock);
    const int32_t at = indexOf(manager, remoteMarker);
    if (at >= 0 && count(manager) < kMaxMarkers) {
        const int32_t total = count(manager);
        std::memmove(&marker(manager, at), &marker(manager, at + 1), static_cast<size_t>(total - at - 1) * sizeof(uintptr_t));
        count(manager) = total - 1;
        for (int type = 0; type < kMarkerTypes; ++type) {
            int32_t& start = ds2::field<int32_t>(manager, kGroupStarts + type * sizeof(int32_t));
            if (start > at) --start;
        }
        const int32_t samAt = indexOf(manager, samMarker);
        if (samAt >= 0) {
            ds2::field<int32_t>(manager, kGroupStarts + kPlayerType * sizeof(int32_t)) = samAt;
            ds2::field<int32_t>(manager, kGroupCounts + kPlayerType * sizeof(int32_t)) = 1;
        }
        logger::write("remote_marker: the remote's player marker taken out of the marker manager (was index %d of %d), "
                      "the player group points at Sam's (index %d)", at, total, samAt);
    }
    ReleaseSRWLockExclusive(lock);
}

}  // namespace remote_marker
