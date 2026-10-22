#include "join_sync.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <utility>

#include "flag_sync.h"
#include "floor_items_sync.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "join_extras.h"
#include "log.h"
#include "net_pad.h"
#include "protocol.h"
#include "room_authority.h"
#include "scene.h"
#include "session_ready.h"
#include "state_mode.h"

namespace {

using Clock = std::chrono::steady_clock;
using character_owner::Character;
using join_sync::CharacterPlace;
using join_sync::JoinSnapshot;

constexpr auto kRequestInterval = std::chrono::seconds(3);

NetClient* g_net = nullptr;

std::mutex g_mutex;
bool g_answerDue = false;                  // guarded by g_mutex: host owes a snapshot
std::optional<JoinSnapshot> g_received;    // guarded by g_mutex: guest's unapplied snapshot

// Game thread, guest only.
uintptr_t g_loadedPair = 0;          // controlled ^ partner of the load the snapshot belongs to
bool g_applied = false;              // the snapshot for g_loadedPair has been applied
std::atomic<bool> g_placed{false};  // guest: applied and placed (read on the net thread)
std::atomic<bool> g_resyncRequested{false};  // guest: take a new snapshot even though one was applied
Clock::time_point g_lastRequest;
std::optional<JoinSnapshot> g_snapshot;    // received, not yet applied
std::optional<JoinSnapshot> g_travelling;  // applied; waiting for the own character's door to arrive

bool inGame() { return game::controlled() && game::partner() && game_state::playing(); }

// The guest's game was loaded again or the link dropped: the placement and, in state mode, the handshake start over.
void forgetPlacement() {
    g_placed = false;
    if (!state_mode::on()) return;
    session_ready::reset();
    join_extras::reset();
}

const CharacterPlace& placeOf(const JoinSnapshot& snapshot, Character character) {
    return snapshot.places[static_cast<size_t>(character)];
}

template <class T>
bool sendValue(uint16_t type, const T& value) {
    return g_net->send(type, true, proto::kSlotAll, proto::bytesOf(value));
}

void answer() {
    JoinSnapshot snapshot{};
    for (const Character character : character_owner::kCharacters) {
        CharacterPlace& place = snapshot.places[static_cast<size_t>(character)];
        const uintptr_t player = character_owner::find(character);
        place.scene = scene::of(player);
        place.hasDoor = door_sync::lastDoor(static_cast<uint8_t>(character), place.door);
        game::readTransform(player, place.pos, place.quat);
        inventory_sync::readBlock(static_cast<uint8_t>(character), snapshot.inventories[static_cast<size_t>(character)]);
    }
    if (!flag_sync::read(snapshot.flags) || !sendValue(proto::kMsgJoinSnapshot, snapshot)) return;
    floor_items_sync::sendJournal();
    game_tick::rearmAfterResync();
    if (state_mode::on()) join_extras::send();
    logger::write("join_sync: snapshot sent (Billy scene 0x%02x, Rebecca scene 0x%02x)",
                  placeOf(snapshot, Character::Billy).scene, placeOf(snapshot, Character::Rebecca).scene);
}

// The host's character joins its room (input mode; in state mode ROOM_MOVE and the pose room self-heal put the puppet there).
void placeHostCharacter(const JoinSnapshot& snapshot) {
    const Character other = character_owner::other(character_owner::localCharacter());
    const CharacterPlace& otherPlace = placeOf(snapshot, other);
    const uintptr_t otherPlayer = character_owner::find(other);
    if (otherPlace.hasDoor && scene::of(otherPlayer) != otherPlace.scene) {
        scene::move(otherPlayer, static_cast<uint16_t>(otherPlace.door.room), otherPlace.door.entry);
    }
}

// The own character stands in its room: the host's character joins its room, the own one takes the host's position.
void finish(const JoinSnapshot& snapshot) {
    if (!state_mode::on()) placeHostCharacter(snapshot);
    const Character own = character_owner::localCharacter();
    const CharacterPlace& ownPlace = placeOf(snapshot, own);
    game::writeTransform(character_owner::find(own), ownPlace.pos, ownPlace.quat);
    g_placed = true;
    game_tick::rearmAfterResync();
    if (state_mode::on()) room_authority::resendClaims();
    logger::write("join_sync: caught up in scene 0x%02x", scene::current());
}

void apply(const JoinSnapshot& snapshot) {
    flag_sync::applySnapshot(snapshot.flags);
    for (const Character character : character_owner::kCharacters) {
        inventory_sync::applySnapshot(static_cast<uint8_t>(character), snapshot.inventories[static_cast<size_t>(character)]);
    }
    g_applied = true;
    if (state_mode::on()) join_extras::onSnapshotApplied();
    const CharacterPlace& own = placeOf(snapshot, character_owner::localCharacter());
    if (own.scene == scene::of(character_owner::find(character_owner::localCharacter())) || !own.hasDoor) {
        if (own.scene != scene::current()) logger::write("join_sync: own room 0x%02x unknown door, staying", own.scene);
        finish(snapshot);
        return;
    }
    door_sync::queue(own.door, true);
    g_travelling = snapshot;
    logger::write("join_sync: travelling to scene 0x%02x", own.scene);
}

// The save may have left the own character in another room than the loaded one: the game's own switch brings the
// camera and the room to it first (it lands over the next frames).
bool ownCharacterLoaded() {
    const Character own = character_owner::localCharacter();
    if (own == Character::Unknown) return false;
    const uintptr_t player = character_owner::find(own);
    if (game_state::inCurrentRoom(player)) return true;
    character_owner::switchTo(own);
    return false;
}

void guestTick() {
    if (!inGame()) return;
    const uintptr_t pair = game::controlled() ^ game::partner();
    if (pair != g_loadedPair) {
        g_loadedPair = pair;
        g_applied = false;
        forgetPlacement();
        g_snapshot.reset();
        g_travelling.reset();
        g_lastRequest = {};
    }
    if (g_resyncRequested.exchange(false) && g_applied) {
        g_applied = false;
        g_snapshot.reset();
        g_travelling.reset();
        g_lastRequest = {};
        logger::write("join_sync: asking for a new snapshot");
    }
    if (g_travelling &&
        placeOf(*g_travelling, character_owner::localCharacter()).scene == scene::current()) {
        finish(*g_travelling);
        g_travelling.reset();
    }
    {
        std::lock_guard lock(g_mutex);
        if (g_received) g_snapshot = std::exchange(g_received, std::nullopt);
    }
    if (g_snapshot && !g_applied && ownCharacterLoaded()) {
        apply(*g_snapshot);
        g_snapshot.reset();
    }
    if (g_applied || g_snapshot || Clock::now() - g_lastRequest < kRequestInterval) return;
    if (sendValue(proto::kMsgSnapshotRequest, scene::current())) g_lastRequest = Clock::now();
}

void hostTick() {
    bool due = false;
    {
        std::lock_guard lock(g_mutex);
        due = std::exchange(g_answerDue, false);
    }
    if (due && inGame()) answer();
}

void onTick() {
    if (!net_pad::active()) {
        forgetPlacement();
        g_loadedPair = 0;
        return;
    }
    if (character_owner::isHost()) hostTick();
    else guestTick();
}

}  // namespace

namespace join_sync {

void requestResync() { g_resyncRequested = true; }

bool placed() { return character_owner::isHost() || g_placed.load(); }

bool caughtUp() {
    if (character_owner::isHost()) return true;
    return state_mode::on() ? session_ready::echoed() : g_placed.load();
}

void onFrame(const GameFrame& frame) {
    std::lock_guard lock(g_mutex);
    if (frame.type == proto::kMsgSnapshotRequest && frame.payload.size() == sizeof(uint16_t)) {
        g_answerDue = true;
        return;
    }
    if (frame.type != proto::kMsgJoinSnapshot || frame.payload.size() != sizeof(JoinSnapshot)) return;
    JoinSnapshot snapshot;
    std::memcpy(&snapshot, frame.payload.data(), sizeof(snapshot));
    g_received = snapshot;
    if (state_mode::on()) join_extras::reset();
}

void enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("join_sync", onTick);
}

}  // namespace join_sync
