#include "split_rooms.h"

#include <optional>

#include "character_owner.h"
#include "door_travel.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "log.h"
#include "net_pad.h"
#include "party_mode.h"
#include "scene.h"

namespace {

using character_owner::Character;
using door_sync::DoorChange;

constexpr uint32_t kParkEntry = 0;  // door entry a peer's character waits at in the loaded room while its room is freed

struct Parked {
    Character character;
    uint16_t scene;
};

std::optional<DoorChange> g_pending;  // game thread only: the newest peer door not yet applied (targets are absolute)
std::optional<Parked> g_parked;       // game thread only: a peer's character taken out of the room a local door loads

void apply(const DoorChange& change) {
    const auto mover = static_cast<Character>(change.characterId);
    const uintptr_t player = character_owner::find(mover);
    if (!player || !character_owner::isRemoteOwned(mover)) {
        logger::write("split_rooms: door of %s dropped, not the peer's character here", character_owner::name(mover));
        return;
    }
    if (scene::move(player, static_cast<uint16_t>(change.room), change.entry)) door_sync::remember(change);
}

// The peer's character that sits alone in the dormant record of `room`, if any.
std::optional<Parked> loneDormantPeer(uint16_t room) {
    if (room == scene::current()) return std::nullopt;
    std::optional<Parked> peer;
    for (const Character character : character_owner::kCharacters) {
        const uintptr_t player = character_owner::find(character);
        if (!player || scene::of(player) != room) continue;
        if (!character_owner::isRemoteOwned(character)) return std::nullopt;
        peer = Parked{character, room};
    }
    return peer;
}

// The peer's character goes back into the room that now loaded fresh, at the entry its own door used.
void returnParked() {
    const Parked parked = *g_parked;
    g_parked.reset();
    const uintptr_t player = character_owner::find(parked.character);
    if (!player || scene::current() != parked.scene || scene::of(player) == parked.scene) return;
    DoorChange door{};
    const bool known = door_sync::lastDoor(static_cast<uint8_t>(parked.character), door) && door.room == parked.scene;
    scene::move(player, parked.scene, known ? door.entry : kParkEntry);
}

void onTick() {
    if (!net_pad::active()) {
        g_pending.reset();
        return;
    }
    // The peer's character must be the partner here; camera_parity puts the camera back on our own one first.
    if (!g_pending || !game_state::playing()) return;
    if (character_owner::identify(game::controlled()) == static_cast<Character>(g_pending->characterId)) return;
    apply(*g_pending);
    g_pending.reset();
}

}  // namespace

namespace split_rooms {

bool takeOver(const DoorChange& change) {
    if (travelsTogether()) return false;  // both characters go through the door
    g_pending = change;
    return true;
}

void beforeLocalDoor(uint16_t room) {
    if (!net_pad::active() || g_parked) return;
    const std::optional<Parked> peer = loneDormantPeer(room);
    if (!peer) return;
    const uintptr_t player = character_owner::find(peer->character);
    if (!scene::move(player, scene::current(), kParkEntry)) return;
    g_parked = peer;
    logger::write("split_rooms: room 0x%x freed so the door loads it fresh (enemies and items spawn)", room);
}

void onArrival() {
    if (g_parked) returnParked();
}

bool apart() {
    if (!net_pad::active()) return false;
    if (door_travel::peerPlace() == door_travel::PeerPlace::Elsewhere) return true;
    // A save that split the two, or a script's switch, can leave this player's own character outside the loaded room
    // while the peer is in it: that is apart too, so the camera goes to the own character and its room loads.
    const uintptr_t own = character_owner::find(character_owner::localCharacter());
    return own && game_state::playing() && !game_state::inCurrentRoom(own);
}

bool travelsTogether() { return !apart() && party_mode::current() == control_rule::PartyMode::Team; }

bool independent() { return apart() || party_mode::current() == control_rule::PartyMode::LeaveBehind; }

bool localEnemyAuthority() { return door_travel::enemyAuthority(); }

void enable() { game_tick::addCallback("split_rooms", onTick); }

}  // namespace split_rooms
