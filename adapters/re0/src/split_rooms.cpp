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

std::optional<DoorChange> g_pending;  // game thread only: the newest peer door not yet applied (targets are absolute)

void apply(const DoorChange& change) {
    const auto mover = static_cast<Character>(change.characterId);
    const uintptr_t player = character_owner::find(mover);
    if (!player || !character_owner::isRemoteOwned(mover)) {
        logger::write("split_rooms: door of %s dropped, not the peer's character here", character_owner::name(mover));
        return;
    }
    if (scene::move(player, static_cast<uint16_t>(change.room), change.entry)) door_sync::remember(change);
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
    // Together in TEAM both characters go through the door.
    if (!apart() && party_mode::current() == control_rule::PartyMode::Team) return false;
    g_pending = change;
    return true;
}

bool apart() {
    if (!net_pad::active()) return false;
    if (door_travel::peerPlace() == door_travel::PeerPlace::Elsewhere) return true;
    // A save that split the two, or a script's switch, can leave this player's own character outside the loaded room
    // while the peer is in it: that is apart too, so the camera goes to the own character and its room loads.
    const uintptr_t own = character_owner::find(character_owner::localCharacter());
    return own && game_state::playing() && !game_state::inCurrentRoom(own);
}

bool independent() { return apart() || party_mode::current() == control_rule::PartyMode::LeaveBehind; }

bool localEnemyAuthority() { return door_travel::enemyAuthority(); }

void enable() { game_tick::addCallback("split_rooms", onTick); }

}  // namespace split_rooms
