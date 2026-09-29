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

namespace {

using door_sync::DoorChange;

using character_owner::Character;
using ChangeRoomFunction = void(__fastcall*)(void* self, void* edx, uint32_t room, uint32_t entry, uint32_t flags);
using ActOnTriggerFunction = bool(__cdecl*)(void* script, void* context);

NetClient* g_net = nullptr;
ChangeRoomFunction g_originalChangeRoom = nullptr;
ActOnTriggerFunction g_originalActOnTrigger = nullptr;
bool g_applying = false;  // game thread only: a peer's door is being run, so the hook passes it through

std::mutex g_mutex;
std::optional<DoorChange> g_pending;  // guarded by g_mutex: the newest peer door not yet run

bool focusedIsLocal() {
    return character_owner::isLocalOwned(character_owner::identify(game::controlled()));
}

// Makes `character` the focused character when it is the partner.
void focus(Character character) {
    const uintptr_t controlled = game::controlled();
    const uintptr_t partner = game::partner();
    if (controlled && partner && character_owner::identify(partner) == character) game::swapControlled(partner, controlled);
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
    focus(own);
    camera_parity::holdLocalFocus();
    logger::write("door_sync: %s acted on a trigger as the partner, focus moved to it", character_owner::name(own));
    return true;
}

void send(const DoorChange& change) {
    g_net->send(proto::kMsgDoorChange, true, proto::kSlotAll, {reinterpret_cast<const uint8_t*>(&change), sizeof(change)});
    debug_stats::count(debug_stats::Counter::DoorsSent);
}

void __fastcall changeRoomDetour(void* self, void* edx, uint32_t room, uint32_t entry, uint32_t flags) {
    if (g_applying || !net_pad::active()) {
        g_originalChangeRoom(self, edx, room, entry, flags);
        return;
    }
    if (!focusedIsLocal()) {
        debug_stats::count(debug_stats::Counter::DoorsSuppressed);
        logger::write("door_sync: suppressed a local door to room 0x%x, the peer owns the focused character", room);
        return;
    }
    g_originalChangeRoom(self, edx, room, entry, flags);
    send({room, entry, flags, static_cast<uint8_t>(character_owner::identify(game::controlled())), {}});
    logger::write("door_sync: door to room 0x%x entry 0x%x flags 0x%x sent", room, entry, flags);
}

std::optional<DoorChange> takePending() {
    std::lock_guard lock(g_mutex);
    return std::exchange(g_pending, std::nullopt);
}

void onTick() {
    if (game_state::doorActive()) return;
    const std::optional<DoorChange> change = takePending();
    const uintptr_t roomControl = game::readPointer(game::kRoomControlGlobal);
    if (!change || !roomControl) return;
    focus(static_cast<Character>(change->characterId));
    g_applying = true;
    g_originalChangeRoom(reinterpret_cast<void*>(roomControl), nullptr, change->room, change->entry, change->flags);
    g_applying = false;
    debug_stats::count(debug_stats::Counter::DoorsApplied);
    logger::write("door_sync: ran the peer's door to room 0x%x entry 0x%x", change->room, change->entry);
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
    std::lock_guard lock(g_mutex);
    g_pending = change;
}

bool enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("door_sync", onTick);
    return hooks::install("sRoomControl::changeRoom", game::kChangeRoomFunction,
                          reinterpret_cast<void*>(&changeRoomDetour), reinterpret_cast<void**>(&g_originalChangeRoom)) &&
           hooks::install("script::actOnTrigger", game::kActOnTriggerFunction,
                          reinterpret_cast<void*>(&actOnTriggerDetour), reinterpret_cast<void**>(&g_originalActOnTrigger));
}

void uninstall() {
    hooks::remove(game::kChangeRoomFunction);
    hooks::remove(game::kActOnTriggerFunction);
}

}  // namespace door_sync
