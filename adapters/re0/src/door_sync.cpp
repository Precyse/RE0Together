#include "door_sync.h"

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <optional>
#include <utility>

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
std::atomic<bool> g_shared{false};  // the running door is played on both machines

std::mutex g_mutex;
std::optional<DoorChange> g_pending;  // guarded by g_mutex: the newest peer door not yet run
bool g_pendingBothTravel = false;     // guarded by g_mutex
std::array<std::optional<DoorChange>, character_owner::kCharacterCount> g_lastDoor;  // guarded by g_mutex

// A door started here also takes the partner when the game's follow flag is set and the partner is in the room.
void rememberWithCarried(const DoorChange& change) {
    door_sync::remember(change);
    const Character partner = character_owner::identify(game::partner());
    uint8_t follow = 0;
    const uintptr_t sPlayer = game::readPointer(game::kPlayerGlobal);
    if (partner == Character::Unknown || !sPlayer || !game::readMemory(sPlayer + game::kPlayerFollowOffset, follow) ||
        !follow || !game_state::inCurrentRoom(game::partner())) {
        return;
    }
    DoorChange carried = change;
    carried.characterId = static_cast<uint8_t>(partner);
    door_sync::remember(carried);
}

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
    logger::write("door_sync: %s acted on a trigger as the partner, focus moved to it", character_owner::name(own));
    return true;
}

void send(const DoorChange& change) {
    g_net->send(proto::kMsgDoorChange, true, proto::kSlotAll, proto::bytesOf(change));
    debug_stats::count(debug_stats::Counter::DoorsSent);
}

void __fastcall doorStartDetour(void* self, void* edx, uint32_t room, uint32_t entry, uint32_t arg3, uint32_t arg4,
                                uint32_t flag) {
    const auto focused = static_cast<uint8_t>(character_owner::identify(game::controlled()));
    rememberWithCarried({room, entry, arg3, arg4, flag, focused, {}});
    if (g_applying || !net_pad::active()) {
        if (!g_applying) g_shared = false;
        g_originalDoorStart(self, edx, room, entry, arg3, arg4, flag);
        return;
    }
    if (!focusedIsLocal()) {
        debug_stats::count(debug_stats::Counter::DoorsSuppressed);
        logger::write("door_sync: suppressed a local door to room 0x%x, the peer owns the focused character", room);
        return;
    }
    split_rooms::beforeLocalDoor(static_cast<uint16_t>(room));
    g_shared = split_rooms::travelsTogether();  // the peer plays it too (split_rooms::takeOver)
    g_originalDoorStart(self, edx, room, entry, arg3, arg4, flag);
    send({room, entry, arg3, arg4, flag, focused, {}});
    logger::write("door_sync: door to room 0x%x entry 0x%x sent", room, entry);
}

std::optional<DoorChange> takePending(bool& bothTravel) {
    std::lock_guard lock(g_mutex);
    bothTravel = std::exchange(g_pendingBothTravel, false);
    return std::exchange(g_pending, std::nullopt);
}

// A peer door waits for plain gameplay here: started in a menu or another screen it would be lost.
void onTick() {
    if (!game_state::playing()) return;
    bool bothTravel = false;
    const std::optional<DoorChange> change = takePending(bothTravel);
    if (!change || (!bothTravel && split_rooms::takeOver(*change))) return;
    g_shared = !bothTravel;  // the peer's own door; a join teleport has no door on the peer
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

bool lastDoor(uint8_t characterId, DoorChange& out) {
    std::lock_guard lock(g_mutex);
    if (characterId >= g_lastDoor.size() || !g_lastDoor[characterId]) return false;
    out = *g_lastDoor[characterId];
    return true;
}

void remember(const DoorChange& change) {
    if (change.characterId >= g_lastDoor.size()) return;
    std::lock_guard lock(g_mutex);
    g_lastDoor[change.characterId] = change;
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

bool sharedDoor() { return g_shared; }

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
