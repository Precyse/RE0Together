#include "floor_items_sync.h"

#include <cstring>
#include <mutex>
#include <vector>

#include "debug_stats.h"
#include "floor_pending.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "hooks.h"
#include "input_redirect.h"
#include "protocol.h"

namespace {

using debug_stats::Counter;
using floor_items_sync::FloorPut;
using floor_items_sync::FloorTake;
using floor_items_sync::Vec3;
using floor_pending::Event;
using floor_pending::kMatchDistance;
using Records = std::array<game::ItemPutRecord, game::kItemPutRecordCount>;

constexpr size_t kMaxIncoming = 64;
constexpr uint32_t kSettleFramesAfterArrival = 30;

using PutFunction = void*(__fastcall*)(void* self, void* edx, const game::ItemDesc* desc, const Vec3* pos,
                                       const Vec3* rot);
using RemoveFunction = void*(__fastcall*)(void* self, void* edx, void* item);

struct Incoming {
    uint16_t room;
    Event event;
};

NetClient* g_net = nullptr;
PutFunction g_originalPut = nullptr;
RemoveFunction g_originalRemove = nullptr;
bool g_applying = false;  // game thread only: a network event is being applied, so the hooks do not broadcast

std::mutex g_mutex;
std::vector<Incoming> g_incoming;  // guarded by g_mutex
floor_pending::Queue g_pending;    // game thread only: events for rooms that are not loaded and settled
uint32_t g_settleFrames = 0;       // game thread only: frames left before the arrived room takes events

bool readRecords(Records& out) {
    const uintptr_t table = game::readPointer(game::kItemPutGlobal);
    return table && game::readMemory(table + game::kItemPutRecordsOffset, out);
}

bool isLive(const game::ItemPutRecord& record) {
    return record.item && record.state != game::kItemPutEmptyState;
}

bool readPosition(uint32_t item, Vec3& out) { return game::readMemory(item + game::kUnitPositionOffset, out); }

float distanceSquared(const Vec3& a, const Vec3& b) {
    float sum = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) sum += (a[i] - b[i]) * (a[i] - b[i]);
    return sum;
}

// The live uItem of the given id nearest to `pos` within kMatchDistance, or 0.
uint32_t findItem(uint32_t itemId, const Vec3& pos) {
    Records records;
    if (!readRecords(records)) return 0;
    uint32_t best = 0;
    float bestDistance = kMatchDistance * kMatchDistance;
    for (const game::ItemPutRecord& record : records) {
        Vec3 itemPos;
        if (!isLive(record) || record.itemId != itemId || !readPosition(record.item, itemPos)) continue;
        const float distance = distanceSquared(itemPos, pos);
        if (distance <= bestDistance) {
            best = record.item;
            bestDistance = distance;
        }
    }
    return best;
}

bool itemIdOf(uint32_t item, uint32_t& itemId) {
    Records records;
    if (!readRecords(records)) return false;
    for (const game::ItemPutRecord& record : records) {
        if (isLive(record) && record.item == item) {
            itemId = record.itemId;
            return true;
        }
    }
    return false;
}

void sendPut(const game::ItemDesc& desc, const Vec3& pos, const Vec3& rot) {
    const FloorPut message{game_state::currentRoom(), 0, desc.itemId, desc.count, pos, rot};
    if (g_net->send(proto::kMsgFloorPut, true, proto::kSlotAll, proto::bytesOf(message))) {
        debug_stats::count(Counter::FloorPutSent);
    }
}

void sendTake(uint32_t itemId, const Vec3& pos) {
    const FloorTake message{game_state::currentRoom(), 0, itemId, pos};
    if (g_net->send(proto::kMsgFloorTake, true, proto::kSlotAll, proto::bytesOf(message))) {
        debug_stats::count(Counter::FloorTakeSent);
    }
}

// A remote-owned character's replayed input must not change the floor here: its own machine decides and announces.
void* __fastcall putDetour(void* self, void* edx, const game::ItemDesc* desc, const Vec3* pos, const Vec3* rot) {
    if (g_applying) return g_originalPut(self, edx, desc, pos, rot);
    if (input_redirect::replayingRemoteInput()) return nullptr;
    game::ItemDesc descCopy{};
    Vec3 posCopy{};
    Vec3 rotCopy{};
    const bool readable = game::readMemory(reinterpret_cast<uintptr_t>(desc), descCopy) &&
                          game::readMemory(reinterpret_cast<uintptr_t>(pos), posCopy) &&
                          game::readMemory(reinterpret_cast<uintptr_t>(rot), rotCopy);
    void* item = g_originalPut(self, edx, desc, pos, rot);
    if (item && readable) sendPut(descCopy, posCopy, rotCopy);
    return item;
}

void* __fastcall removeDetour(void* self, void* edx, void* item) {
    if (g_applying) return g_originalRemove(self, edx, item);
    if (input_redirect::replayingRemoteInput()) return nullptr;
    const uint32_t address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(item));
    uint32_t itemId = 0;
    Vec3 pos{};
    const bool known = itemIdOf(address, itemId) && readPosition(address, pos);
    void* result = g_originalRemove(self, edx, item);
    if (known) sendTake(itemId, pos);
    return result;
}

void applyPut(const Event& event) {
    const uintptr_t table = game::readPointer(game::kItemPutGlobal);
    const game::ItemDesc desc{event.itemId, event.count, 0};
    g_applying = true;
    void* item = game::callThiscall<void*>(game::kItemPutFunction, reinterpret_cast<void*>(table), &desc, &event.pos,
                                           &event.rot);
    g_applying = false;
    if (item) debug_stats::count(Counter::FloorPutApplied);
}

void applyTake(const Event& event) {
    const uint32_t item = findItem(event.itemId, event.pos);
    if (!item) {
        debug_stats::count(Counter::FloorTakeMisses);
        return;
    }
    g_applying = true;
    game::callThiscall<void*>(game::kItemRemoveFunction, nullptr, reinterpret_cast<void*>(static_cast<uintptr_t>(item)));
    g_applying = false;
    debug_stats::count(Counter::FloorTakeApplied);
}

void apply(const Event& event) {
    if (event.isTake) applyTake(event);
    else applyPut(event);
}

// The loaded room is stable and the game can take a put: no door running, settled after arrival, item table and a
// controlled character present.
bool roomReady() {
    return g_settleFrames == 0 && !game_state::doorActive() && game::readPointer(game::kItemPutGlobal) &&
           game::controlled();
}

void applyPending(uint16_t room) {
    for (const Event& event : g_pending.take(room)) {
        apply(event);
        debug_stats::count(Counter::FloorPendingApplied);
    }
}

void onTick() {
    std::vector<Incoming> incoming;
    {
        std::lock_guard lock(g_mutex);
        incoming.swap(g_incoming);
    }
    if (g_settleFrames > 0 && !game_state::doorActive()) --g_settleFrames;
    const uint16_t room = game_state::currentRoom();
    const bool ready = roomReady();
    if (ready) applyPending(room);
    for (const Incoming& item : incoming) {
        if (ready && item.room == room) {
            apply(item.event);
            continue;
        }
        const floor_pending::AddResult result = g_pending.add(item.room, item.event);
        if (result.droppedOldest) debug_stats::count(Counter::FloorPendingDropped);
    }
    debug_stats::set(debug_stats::Gauge::FloorPending, static_cast<int>(g_pending.total()));
}

void enqueue(uint16_t room, const Event& event) {
    std::lock_guard lock(g_mutex);
    if (g_incoming.size() < kMaxIncoming) g_incoming.push_back({room, event});
}

}  // namespace

namespace floor_items_sync {

void onFrame(const GameFrame& frame) {
    if (frame.type == proto::kMsgFloorPut && frame.payload.size() == sizeof(FloorPut)) {
        FloorPut message;
        std::memcpy(&message, frame.payload.data(), sizeof(message));
        enqueue(message.room, {false, message.itemId, message.count, message.pos, message.rot});
    } else if (frame.type == proto::kMsgFloorTake && frame.payload.size() == sizeof(FloorTake)) {
        FloorTake message;
        std::memcpy(&message, frame.payload.data(), sizeof(message));
        enqueue(message.room, {true, message.itemId, 0, message.pos, {}});
    }
}

void onArrival() { g_settleFrames = kSettleFramesAfterArrival; }

bool enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("floor_items_sync", onTick);
    return hooks::install("sItemPut::put", game::kItemPutFunction, reinterpret_cast<void*>(&putDetour),
                          reinterpret_cast<void**>(&g_originalPut)) &&
           hooks::install("sItemPut::remove", game::kItemRemoveFunction, reinterpret_cast<void*>(&removeDetour),
                          reinterpret_cast<void**>(&g_originalRemove));
}

void uninstall() {
    hooks::remove(game::kItemPutFunction);
    hooks::remove(game::kItemRemoveFunction);
}

}  // namespace floor_items_sync
