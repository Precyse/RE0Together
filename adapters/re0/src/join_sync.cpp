#include "join_sync.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <utility>

#include "character_owner.h"
#include "flag_sync.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "inventory_sync.h"
#include "log.h"
#include "net_pad.h"
#include "protocol.h"

namespace {

using Clock = std::chrono::steady_clock;
using character_owner::Character;
using join_sync::JoinSnapshot;

constexpr auto kRequestInterval = std::chrono::seconds(3);

NetClient* g_net = nullptr;

std::mutex g_mutex;
bool g_answerDue = false;                  // guarded by g_mutex: host owes a snapshot
std::optional<JoinSnapshot> g_received;    // guarded by g_mutex: guest's unapplied snapshot

// Game thread, guest only.
uintptr_t g_loadedPair = 0;          // controlled ^ partner of the load the snapshot belongs to
bool g_applied = false;              // the snapshot for g_loadedPair has been applied
std::atomic<bool> g_caughtUp{false};  // guest: applied and in the host's room (read on the net thread)
Clock::time_point g_lastRequest;
std::optional<JoinSnapshot> g_waitingForRoom;  // applied except Billy's position, which waits for the host's room

bool inGame() {
    return game::controlled() && game::partner() && !game_state::doorActive() &&
           game_state::currentRoom() != game_state::kRoomLoading;
}

template <class T>
bool sendValue(uint16_t type, const T& value) {
    return g_net->send(type, true, proto::kSlotAll, proto::bytesOf(value));
}

void answer() {
    JoinSnapshot snapshot{};
    snapshot.hostRoom = game_state::currentRoom();
    snapshot.hasDoor = door_sync::lastDoor(snapshot.door);
    const uintptr_t billy = character_owner::find(Character::Billy);
    snapshot.billyInRoom = game_state::inCurrentRoom(billy) && game::readTransform(billy, snapshot.billyPos, snapshot.billyQuat);
    for (uint8_t id = 0; id < character_owner::kCharacterCount; ++id) inventory_sync::readBlock(id, snapshot.inventories[id]);
    if (!flag_sync::read(snapshot.flags) || !sendValue(proto::kMsgJoinSnapshot, snapshot)) return;
    logger::write("join_sync: snapshot sent (room 0x%04x, door %s)", snapshot.hostRoom, snapshot.hasDoor ? "known" : "none");
}

void placeBilly(const JoinSnapshot& snapshot) {
    g_caughtUp = true;
    if (snapshot.billyInRoom) game::writeTransform(character_owner::find(Character::Billy), snapshot.billyPos, snapshot.billyQuat);
    logger::write("join_sync: in the host's room 0x%04x", snapshot.hostRoom);
}

void apply(const JoinSnapshot& snapshot) {
    flag_sync::applySnapshot(snapshot.flags);
    for (uint8_t id = 0; id < character_owner::kCharacterCount; ++id) inventory_sync::applySnapshot(id, snapshot.inventories[id]);
    g_applied = true;
    if (snapshot.hostRoom == game_state::currentRoom()) {
        placeBilly(snapshot);
        return;
    }
    if (snapshot.hasDoor) {
        door_sync::queue(snapshot.door, true);
        logger::write("join_sync: teleporting to room 0x%04x", snapshot.hostRoom);
        g_waitingForRoom = snapshot;
        return;
    }
    // Without a known door the guest cannot follow; it plays on from the save's room.
    g_caughtUp = true;
    logger::write("join_sync: host room 0x%04x differs and no door is known", snapshot.hostRoom);
}

void guestTick() {
    if (!inGame()) return;
    const uintptr_t pair = game::controlled() ^ game::partner();
    if (pair != g_loadedPair) {
        g_loadedPair = pair;
        g_applied = false;
        g_caughtUp = false;
        g_waitingForRoom.reset();
        g_lastRequest = {};
    }
    if (g_waitingForRoom && g_waitingForRoom->hostRoom == game_state::currentRoom()) {
        placeBilly(*g_waitingForRoom);
        g_waitingForRoom.reset();
    }
    std::optional<JoinSnapshot> received;
    {
        std::lock_guard lock(g_mutex);
        received.swap(g_received);
    }
    if (received && !g_applied) apply(*received);
    if (g_applied || Clock::now() - g_lastRequest < kRequestInterval) return;
    const uint16_t room = game_state::currentRoom();
    if (sendValue(proto::kMsgSnapshotRequest, room)) g_lastRequest = Clock::now();
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
        g_caughtUp = false;
        g_loadedPair = 0;
        return;
    }
    if (character_owner::isHost()) hostTick();
    else guestTick();
}

}  // namespace

namespace join_sync {

bool caughtUp() { return character_owner::isHost() || g_caughtUp.load(); }

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
}

void enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("join_sync", onTick);
}

}  // namespace join_sync
