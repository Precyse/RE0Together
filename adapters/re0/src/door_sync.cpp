#include "door_sync.h"

#include <cstring>
#include <mutex>
#include <optional>
#include <utility>

#include "camera_parity.h"
#include "character_owner.h"
#include "debug_stats.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "hooks.h"
#include "log.h"
#include "net_pad.h"
#include "protocol.h"
#include "split_rooms.h"

namespace {

using door_sync::DoorChange;

using character_owner::Character;
using DoorStartFunction = void(__fastcall*)(void* self, void* edx, uint32_t room, uint32_t entry, uint32_t arg3,
                                           uint32_t arg4, uint32_t flag);
using ActOnTriggerFunction = bool(__cdecl*)(void* script, void* context);

NetClient* g_net = nullptr;
DoorStartFunction g_originalDoorStart = nullptr;
ActOnTriggerFunction g_originalActOnTrigger = nullptr;
bool g_applying = false;  // game thread only: a peer's door is being run, so the hook passes it through

std::mutex g_mutex;
std::optional<DoorChange> g_pending;  // guarded by g_mutex: the newest peer door not yet run
bool g_pendingBothTravel = false;     // guarded by g_mutex
std::optional<DoorChange> g_lastDoor;  // guarded by g_mutex: the door into the current room

bool focusedIsLocal() {
    return character_owner::isLocalOwned(character_owner::identify(game::controlled()));
}

// Runs the game's check as if `player` were the focused character: the check reads sPlayer +0x2c throughout.
bool actsAs(uintptr_t player, void* script, void* context) {
    const uintptr_t sPlayer = game::readPointer(game::kPlayerGlobal);
    const uintptr_t focused = game::controlled();
    if (!sPlayer || !game::writeMemory(sPlayer + game::kPlayerControlledOffset, static_cast<uint32_t>(player))) return false;
    const bool acts = g_originalActOnTrigger(script, context);
    game::writeMemory(sPlayer + game::kPlayerControlledOffset, static_cast<uint32_t>(focused));
    return acts;
}

// With a peer, local pad 0 belongs to this machine's own character: the focused one when it is ours, otherwise the
// partner, which then takes the camera so its door or interaction runs as the focused character's.
bool __cdecl actOnTriggerDetour(void* script, void* context) {
    if (!net_pad::active() || focusedIsLocal()) return g_originalActOnTrigger(script, context);
    const uintptr_t partner = game::partner();
    const Character own = character_owner::identify(partner);
    if (!partner || !character_owner::isLocalOwned(own) || !actsAs(partner, script, context)) return false;
    character_owner::focus(own);
    camera_parity::holdLocalFocus();
    logger::write("door_sync: %s acted on a trigger as the partner, focus moved to it", character_owner::name(own));
    return true;
}

void send(const DoorChange& change) {
    g_net->send(proto::kMsgDoorChange, true, proto::kSlotAll, {reinterpret_cast<const uint8_t*>(&change), sizeof(change)});
    debug_stats::count(debug_stats::Counter::DoorsSent);
}

void remember(const DoorChange& change) {
    std::lock_guard lock(g_mutex);
    g_lastDoor = change;
}

void __fastcall doorStartDetour(void* self, void* edx, uint32_t room, uint32_t entry, uint32_t arg3, uint32_t arg4,
                                uint32_t flag) {
    const auto focused = static_cast<uint8_t>(character_owner::identify(game::controlled()));
    remember({room, entry, arg3, arg4, flag, focused, {}});
    if (g_applying || !net_pad::active()) {
        g_originalDoorStart(self, edx, room, entry, arg3, arg4, flag);
        return;
    }
    if (!focusedIsLocal()) {
        debug_stats::count(debug_stats::Counter::DoorsSuppressed);
        logger::write("door_sync: suppressed a local door to room 0x%x, the peer owns the focused character", room);
        return;
    }
    g_originalDoorStart(self, edx, room, entry, arg3, arg4, flag);
    send({room, entry, arg3, arg4, flag, focused, {}});
    logger::write("door_sync: door to room 0x%x entry 0x%x sent", room, entry);
}

std::optional<DoorChange> takePending(bool& bothTravel) {
    std::lock_guard lock(g_mutex);
    bothTravel = std::exchange(g_pendingBothTravel, false);
    return std::exchange(g_pending, std::nullopt);
}

void onTick() {
    if (game_state::doorActive()) return;
    bool bothTravel = false;
    const std::optional<DoorChange> change = takePending(bothTravel);
    if (!change || (!bothTravel && split_rooms::takeOver(*change))) return;
    character_owner::focus(static_cast<Character>(change->characterId));
    door_sync::run(*change);
}

}  // namespace

namespace door_sync {

void onFrame(const GameFrame& frame) {
    if (frame.type != proto::kMsgDoorChange || frame.payload.size() != sizeof(DoorChange) ||
        frame.slot != net_pad::peerSlot()) {
        return;
    }
    DoorChange change;
    std::memcpy(&change, frame.payload.data(), sizeof(change));
    queue(change);
}

bool lastDoor(DoorChange& out) {
    std::lock_guard lock(g_mutex);
    if (!g_lastDoor) return false;
    out = *g_lastDoor;
    return true;
}

void queue(const DoorChange& change, bool bothTravel) {
    std::lock_guard lock(g_mutex);
    g_pending = change;
    g_pendingBothTravel = bothTravel;
}

void run(const DoorChange& change) {
    const uintptr_t doorLoad = game::readPointer(game::kDoorLoadGlobal);
    if (!doorLoad) return;
    g_applying = true;
    g_originalDoorStart(reinterpret_cast<void*>(doorLoad), nullptr, change.room, change.entry, change.arg3, change.arg4,
                        change.flag);
    g_applying = false;
    debug_stats::count(debug_stats::Counter::DoorsApplied);
    logger::write("door_sync: ran the peer's door to room 0x%x entry 0x%x", change.room, change.entry);
}

bool enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("door_sync", onTick);
    return hooks::install("sDoorLoad::start", game::kDoorStartFunction, reinterpret_cast<void*>(&doorStartDetour),
                          reinterpret_cast<void**>(&g_originalDoorStart)) &&
           hooks::install("script::actOnTrigger", game::kActOnTriggerFunction,
                          reinterpret_cast<void*>(&actOnTriggerDetour), reinterpret_cast<void**>(&g_originalActOnTrigger));
}

void uninstall() {
    hooks::remove(game::kDoorStartFunction);
    hooks::remove(game::kActOnTriggerFunction);
}

}  // namespace door_sync
