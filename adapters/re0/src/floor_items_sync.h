#pragma once
#include <cstdint>

#include "floor_pending.h"
#include "net_client.h"

namespace floor_items_sync {

using Vec3 = floor_pending::Vec3;

// Wire payload of FLOOR_PUT (0x0107), reliable, to all: a floor item was dropped or added. `rot` is the Euler
// rotation the game passes to sItemPut::put.
struct FloorPut {
    uint16_t room;  // stage << 8 | room
    uint16_t reserved;
    uint32_t itemId;
    uint32_t count;
    Vec3 pos;
    Vec3 rot;
};
static_assert(sizeof(FloorPut) == 36);

// Wire payload of FLOOR_TAKE (0x0108), reliable, to all: the floor item at `pos` was picked up.
struct FloorTake {
    uint16_t room;
    uint16_t reserved;
    uint32_t itemId;
    Vec3 pos;
};
static_assert(sizeof(FloorTake) == 20);

// Net thread: queues a peer's event for the game thread.
void onFrame(const GameFrame& frame);

// Game thread: a door transition finished. Pending events of the new room apply once it has settled.
void onArrival();

// Hooks sItemPut::put and remove and registers the per-frame applier. Call before game_tick::install.
bool enable(NetClient& net);

void uninstall();

}  // namespace floor_items_sync
