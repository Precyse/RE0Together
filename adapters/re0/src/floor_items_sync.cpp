#include "floor_items_sync.h"

#include <intrin.h>

#include <cstring>
#include <mutex>
#include <vector>

#include "debug_stats.h"
#include "equip_steps.h"
#include "floor_pending.h"
#include "floor_snapshot.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "hooks.h"
#include "input_redirect.h"
#include "item_ledger.h"
#include "log.h"
#include "protocol.h"
#include "puppet.h"
#include "settled_copy.h"
#include "state_mode.h"
#include "stream_clock.h"
#include "world_epoch.h"

namespace {

using debug_stats::Counter;
using floor_items_sync::FloorPut;
using floor_items_sync::FloorTake;
using floor_items_sync::Vec3;
using floor_pending::Event;
using floor_pending::kMatchDistance;
using Records = std::array<game::ItemPutRecord, game::kItemPutRecordCount>;

constexpr size_t kMaxIncoming = floor_snapshot::kMaxEvents + 64;  // a snapshot arrives in one burst
constexpr uint32_t kSettleFramesAfterArrival = 30;

using PutFunction = void*(__fastcall*)(void* self, void* edx, const game::ItemDesc* desc, const Vec3* pos,
                                       const Vec3* rot);
using RemoveFunction = void*(__fastcall*)(void* self, void* edx, void* item);

struct Incoming {
    uint16_t room;
    Event event;
    uint32_t dueMs;  // monotonic ms from which the event applies (state mode: a puppet's take waits one render delay)
};

NetClient* g_net = nullptr;
PutFunction g_originalPut = nullptr;
RemoveFunction g_originalRemove = nullptr;
bool g_applying = false;  // game thread only: a network event is being applied, so the hooks do not broadcast

std::mutex g_mutex;
std::vector<Incoming> g_incoming;  // guarded by g_mutex
floor_pending::Queue g_pending;    // game thread only: events for rooms that are not loaded and settled
floor_pending::Queue g_journal;    // game thread only: floor changes since the last save or load, for a joining guest
uint32_t g_settleFrames = 0;       // game thread only: frames left before the arrived room takes events
std::vector<Incoming> g_delayed;   // game thread only, state mode: arrived events waiting for their time, in order

// The loaded room's events, in order, held until the floor models their puts requested have loaded.
struct Held {
    uint16_t room = 0;
    std::vector<Event> events;
    uint64_t sinceMs = 0;
    bool waited = false;  // a tick ended with the models still loading
};
Held g_held;  // game thread only

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

// The live uItem of the given id nearest to `pos` within `radius` (kMatchDistance by default), or 0.
uint32_t findItem(uint32_t itemId, const Vec3& pos, float radius = kMatchDistance) {
    Records records;
    if (!readRecords(records)) return 0;
    uint32_t best = 0;
    float bestDistance = radius * radius;
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

item_ledger::Key ledgerKey(uint16_t room, uint32_t itemId, const Vec3& pos) { return {room, itemId, pos}; }

void sendPut(const game::ItemDesc& desc, const Vec3& pos, const Vec3& rot) {
    const FloorPut message{game_state::currentRoom(), world_epoch::stamp(), desc.itemId, desc.count, pos, rot};
    if (g_net->send(proto::kMsgFloorPut, true, proto::kSlotAll, proto::bytesOf(message))) {
        debug_stats::count(Counter::FloorPutSent);
    }
}

void sendTake(uint32_t itemId, const Vec3& pos) {
    const FloorTake message{game_state::currentRoom(), world_epoch::stamp(), itemId, pos};
    if (g_net->send(proto::kMsgFloorTake, true, proto::kSlotAll, proto::bytesOf(message))) {
        debug_stats::count(Counter::FloorTakeSent);
    }
}

// State mode: an item of any source (own drop, room script, a peer's put applied here) puts a new item on the floor, so
// the ledger forgets what it knew about that spot.
void noteLedgerPut(const game::ItemDesc* desc, const Vec3* pos) {
    game::ItemDesc descCopy{};
    Vec3 posCopy{};
    if (game::readMemory(reinterpret_cast<uintptr_t>(desc), descCopy) &&
        game::readMemory(reinterpret_cast<uintptr_t>(pos), posCopy)) {
        item_ledger::notePut(ledgerKey(game_state::currentRoom(), descCopy.itemId, posCopy));
    }
}

// A remote-owned character's replayed input must not change the floor here: its own machine decides and announces.
// The room script puts the map items itself on every machine that loads the room: announcing those would duplicate
// them for a peer already there (and a pending put would duplicate them at its next load). Only their pickup crosses.
void* __fastcall putDetour(void* self, void* edx, const game::ItemDesc* desc, const Vec3* pos, const Vec3* rot) {
    if (state_mode::on()) noteLedgerPut(desc, pos);
    if (g_applying || reinterpret_cast<uintptr_t>(_ReturnAddress()) == game::kScriptItemPutReturn) {
        return g_originalPut(self, edx, desc, pos, rot);
    }
    if (input_redirect::replayingRemoteInput()) return nullptr;
    game::ItemDesc descCopy{};
    Vec3 posCopy{};
    Vec3 rotCopy{};
    const bool readable = game::readMemory(reinterpret_cast<uintptr_t>(desc), descCopy) &&
                          game::readMemory(reinterpret_cast<uintptr_t>(pos), posCopy) &&
                          game::readMemory(reinterpret_cast<uintptr_t>(rot), rotCopy);
    void* item = g_originalPut(self, edx, desc, pos, rot);
    if (item && readable) {
        sendPut(descCopy, posCopy, rotCopy);
        g_journal.add(game_state::currentRoom(), {false, descCopy.itemId, descCopy.count, posCopy, rotCopy});
    }
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
    if (known) {
        sendTake(itemId, pos);
        g_journal.add(game_state::currentRoom(), {true, itemId, 0, pos, {}});
        if (state_mode::on()) item_ledger::noteTaken(ledgerKey(game_state::currentRoom(), itemId, pos));
    }
    return result;
}

void applyPut(const Event& event) {
    if (event.onlyIfAbsent && findItem(event.itemId, event.pos)) return;
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

// The drop's model step for a peer's puts: the set of each item is requested the way the drop menu does, because put
// alone leaves an item this machine never loaded (a weapon carried since the start) without a model.
void requestModels(const std::vector<Event>& events) {
    void* chara = equip_steps::gameChara();
    uint32_t record = 0;
    if (!chara || !game::readMemory(game::controlled() + game::kPlayerSceneRecordOffset, record)) return;
    for (const uint32_t itemId : floor_pending::putItemIds(events)) {
        equip_steps::guarded(
            [&] { equip_steps::requestSet(chara, game::kItemUnitKind, static_cast<int32_t>(itemId), record); });
    }
}

// A fault in the load check lets the events apply rather than hold the room's floor forever.
bool modelsLoaded() {
    bool loaded = false;
    return !equip_steps::guarded([&] { loaded = equip_steps::requestsLoaded(); }) || loaded;
}

// Events for the loaded room join the held ones, their puts' models requested.
void hold(uint16_t room, std::vector<Event> events) {
    if (events.empty()) return;
    requestModels(events);
    if (g_held.events.empty()) g_held = {room, {}, monotonicMs(), false};
    g_held.events.insert(g_held.events.end(), events.begin(), events.end());
}

// The room changed under held events: they wait in their room's queue again.
void requeueHeld() {
    for (const Event& event : g_held.events) g_pending.add(g_held.room, event);
    g_held = Held{};
}

void applyHeld() {
    for (const Event& event : g_held.events) apply(event);
    if (g_held.waited) {
        logger::write("floor_items_sync: %zu floor changes applied after %llu ms of item model loading",
                      g_held.events.size(), monotonicMs() - g_held.sinceMs);
    }
    g_held = Held{};
}

// State mode: the arrived events join the ones still waiting; those whose time has come are returned in order. An event
// that has to wait holds back everything that arrived after it.
std::vector<Incoming> releaseDue(std::vector<Incoming> arrived) {
    g_delayed.insert(g_delayed.end(), arrived.begin(), arrived.end());
    const uint32_t now = static_cast<uint32_t>(monotonicMs());
    size_t due = 0;
    while (due < g_delayed.size() && !stream_clock::after(g_delayed[due].dueMs, now)) ++due;
    std::vector<Incoming> released(g_delayed.begin(), g_delayed.begin() + static_cast<std::ptrdiff_t>(due));
    g_delayed.erase(g_delayed.begin(), g_delayed.begin() + static_cast<std::ptrdiff_t>(due));
    return released;
}

void onTick() {
    std::vector<Incoming> incoming;
    {
        std::lock_guard lock(g_mutex);
        incoming.swap(g_incoming);
    }
    if (state_mode::on()) incoming = releaseDue(std::move(incoming));
    if (g_settleFrames > 0 && !game_state::doorActive()) --g_settleFrames;
    const uint16_t room = game_state::currentRoom();
    const bool ready = roomReady();
    if (!g_held.events.empty() && g_held.room != room) requeueHeld();
    std::vector<Event> arrived;
    if (ready) {
        arrived = g_pending.take(room);
        debug_stats::count(Counter::FloorPendingApplied, static_cast<uint32_t>(arrived.size()));
    }
    for (const Incoming& item : incoming) {
        g_journal.add(item.room, item.event);
        if (ready && item.room == room) {
            arrived.push_back(item.event);
            continue;
        }
        const floor_pending::AddResult result = g_pending.add(item.room, item.event);
        if (result.droppedOldest) debug_stats::count(Counter::FloorPendingDropped);
    }
    hold(room, std::move(arrived));
    if (ready && !g_held.events.empty() && modelsLoaded()) applyHeld();
    if (!g_held.events.empty()) g_held.waited = true;
    debug_stats::set(debug_stats::Gauge::FloorPending, static_cast<int>(g_pending.total() + g_held.events.size()));
}

void enqueue(uint16_t room, const Event& event, uint32_t delayMs = 0) {
    const uint32_t dueMs = static_cast<uint32_t>(monotonicMs()) + delayMs;
    std::lock_guard lock(g_mutex);
    if (g_incoming.size() < kMaxIncoming) g_incoming.push_back({room, event, dueMs});
}

// State mode: a take by the owner of a puppet applies one render delay after it arrives, when the puppet shown at that
// instant reaches for the item (FLOOR_TAKE carries no sender clock; the transit is small against the delay).
uint32_t takeDelayMs(uint8_t slot) {
    if (!state_mode::on()) return 0;
    const auto clock = puppet::clockFor(slot);
    return clock && clock->ready() ? clock->delayMs() : 0;
}

}  // namespace

namespace floor_items_sync {

void onFrame(const GameFrame& frame) {
    if (frame.type == proto::kMsgFloorPut && frame.payload.size() == sizeof(FloorPut)) {
        FloorPut message;
        std::memcpy(&message, frame.payload.data(), sizeof(message));
        if (!world_epoch::acceptsFromGuest(message.worldEpoch, "floor put")) return;
        enqueue(message.room, {false, message.itemId, message.count, message.pos, message.rot});
    } else if (frame.type == proto::kMsgFloorTake && frame.payload.size() == sizeof(FloorTake)) {
        FloorTake message;
        std::memcpy(&message, frame.payload.data(), sizeof(message));
        if (!world_epoch::acceptsFromGuest(message.worldEpoch, "floor take")) return;
        enqueue(message.room, {true, message.itemId, 0, message.pos, {}}, takeDelayMs(frame.slot));
        if (state_mode::on()) item_ledger::noteTaken(ledgerKey(message.room, message.itemId, message.pos));
    } else if (frame.type == proto::kMsgFloorSnapshot) {
        const auto events = floor_snapshot::decode(frame.payload);
        if (!events) return;
        for (const floor_pending::RoomEvent& item : *events) enqueue(item.room, item.event);
    }
}

void onSave() {
    g_journal.clear();
    if (state_mode::on()) item_ledger::clear();
}

void onLoad() {
    onSave();
    g_pending.clear();
    g_delayed.clear();
    g_held = Held{};
    std::lock_guard lock(g_mutex);
    g_incoming.clear();
}

bool locateItem(uint32_t itemId, const Vec3& around, float reach, Vec3& out) {
    const uint32_t item = findItem(itemId, around, reach);
    return item && readPosition(item, out);
}


void sendJournal() {
    const std::vector<floor_pending::RoomEvent> events = g_journal.all();
    if (events.empty()) return;
    g_net->send(proto::kMsgFloorSnapshot, true, proto::kSlotAll, floor_snapshot::encode(events));
    logger::write("floor_items_sync: %zu floor changes sent to the joining guest", events.size());
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
