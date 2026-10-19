#include "room_move.h"

#include <mutex>
#include <optional>

#include "character_owner.h"
#include "debug_stats.h"
#include "door_follow.h"
#include "frame_router.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "hooks.h"
#include "log.h"
#include "net_pad.h"
#include "pose_wire.h"
#include "puppet.h"
#include "room_move_rule.h"
#include "room_move_wire.h"
#include "scene.h"
#include "state_net.h"

namespace {

using character_owner::Character;
using debug_stats::Counter;
using room_move_wire::RoomMove;

using ChangeRoomFunction = void(__fastcall*)(void* self, void* edx, uint32_t sceneId, uint32_t entry, uint32_t flags);

constexpr uint32_t kHealEntry = 0;  // the entry spot a healed puppet waits at until its owner's pose places it

ChangeRoomFunction g_originalChangeRoom = nullptr;

std::mutex g_mutex;
room_move_rule::MoveInbox g_inbox;  // guarded by g_mutex

// Game thread only.
uint32_t g_lastSeq = 0;
uint16_t g_announced = scene::kNone;  // the room the own character was last announced in
bool g_wasLinked = false;
room_move_rule::SelfHeal g_heal;

// Net thread: ROOM_MOVE from the driving peer, naming a character it owns.
void onFrame(const GameFrame& frame) {
    const auto move = room_move_wire::decodeMove(frame.payload);
    if (!move || static_cast<int>(frame.slot) != net_pad::peerSlot() ||
        !character_owner::isOwnedBy(static_cast<Character>(move->character), frame.slot)) {
        logger::writeUnlessRepeated("room_move: ROOM_MOVE from slot %u refused", frame.slot);
        return;
    }
    std::lock_guard lock(g_mutex);
    g_inbox.offer(*move);
}

void sendMove(uint16_t sceneId, uint32_t entry, uint8_t mode) {
    const Character own = character_owner::localCharacter();
    if (own == Character::Unknown) return;
    if (++g_lastSeq == 0) ++g_lastSeq;
    const RoomMove move{g_lastSeq, sceneId, static_cast<uint16_t>(entry), scene::loadSerial(), static_cast<uint8_t>(own), mode};
    if (!state_net::send(room_move_wire::kMsgRoomMove, true, proto::bytesOf(move))) return;
    g_announced = sceneId;
    debug_stats::count(Counter::RoomMovesSent);
    logger::write("room_move: sent %s -> scene 0x%02x entry %u spot %u serial %u", character_owner::name(own), sceneId, entry,
                  mode, move.loadSerial);
}

// The own character's door committed into `sceneId`: the partner follows if it was asked to, then the peer is told.
void onOwnRoomChange(uint32_t sceneId, uint32_t entry) {
    const Character own = character_owner::localCharacter();
    if (!net_pad::active() || own == Character::Unknown || character_owner::identify(game::controlled()) != own ||
        !room_move_wire::sceneValid(sceneId) || entry > room_move_wire::kMaxEntry) {
        return;
    }
    const uint8_t spot = door_follow::onOwnRoomChange(static_cast<uint16_t>(sceneId), entry);
    scene::observeLoad();
    sendMove(static_cast<uint16_t>(sceneId), entry, spot);
}

void __fastcall changeRoomDetour(void* self, void* edx, uint32_t sceneId, uint32_t entry, uint32_t flags) {
    g_originalChangeRoom(self, edx, sceneId, entry, flags);
    onOwnRoomChange(sceneId, entry);
}

// A script, a join or a zap put the own character in a room without a door: the peer is told as well.
void announcePlacement() {
    const uint16_t here = scene::of(character_owner::localPlayer());
    if (!room_move_wire::sceneValid(here) || here == g_announced) return;
    sendMove(here, kHealEntry, room_move_wire::kAutoSpot);
}

// Moves the puppet into the room of the pending ROOM_MOVE once the game is in plain gameplay.
void applyPending() {
    if (!game_state::playing()) return;
    std::optional<RoomMove> move;
    {
        std::lock_guard lock(g_mutex);
        move = g_inbox.pending();
    }
    if (!move) return;
    const auto character = static_cast<Character>(move->character);
    const uintptr_t player = character_owner::find(character);
    if (!player || player == game::controlled()) return;  // the camera character is not the puppet yet: wait
    const uint16_t before = scene::of(player);
    const room_move_rule::Kind kind = room_move_rule::classify(move->scene, scene::current(), before);
    const std::optional<uint32_t> spot =
        move->mode == room_move_wire::kAutoSpot ? std::nullopt : std::optional<uint32_t>(move->mode);
    if (before != move->scene && !scene::move(player, move->scene, move->entry, spot)) return;
    {
        std::lock_guard lock(g_mutex);
        g_inbox.consume();
    }
    g_heal.noteMove();
    debug_stats::count(Counter::RoomMovesApplied);
    logger::write("room_move: %s -> scene 0x%02x entry %u (%s) serial %u", character_owner::name(character), move->scene,
                  move->entry, room_move_rule::name(kind), move->loadSerial);
}

// The owner's poses name another room than the puppet's record, and no ROOM_MOVE explains it.
void healPuppet() {
    const Character local = character_owner::localCharacter();
    if (local == Character::Unknown) return;
    const Character remote = character_owner::other(local);
    const uintptr_t player = character_owner::find(remote);
    if (!player || player == game::controlled()) return;
    const uint16_t poseRoom = puppet::ownerRoom();
    if (!g_heal.due(poseRoom, scene::of(player), game_state::playing(), pose_wire::clockMs())) return;
    if (!scene::move(player, poseRoom, kHealEntry)) return;
    debug_stats::count(Counter::RoomMoveHeals);
    logger::write("room_move: %s self-heal to scene 0x%02x (the owner's poses name it, no ROOM_MOVE for %u ms)",
                  character_owner::name(remote), poseRoom, room_move_rule::kSelfHealMs);
}

void onTick() {
    const bool linked = net_pad::active();
    if (!linked) {
        if (g_wasLinked) {
            std::lock_guard lock(g_mutex);
            g_inbox.clear();
        }
        g_wasLinked = false;
        return;
    }
    if (!g_wasLinked) g_announced = scene::kNone;  // a peer that just arrived learns where this character is
    g_wasLinked = true;
    announcePlacement();
    applyPending();
    healPuppet();
}

}  // namespace

namespace room_move {

bool enable() {
    if (!frame_router::add(room_move_wire::kMsgRoomMove, frame_router::SenderPolicy::Any, onFrame)) return false;
    if (!hooks::install("sRoomControl::changeRoom", game::kChangeRoomFunction, reinterpret_cast<void*>(&changeRoomDetour),
                        reinterpret_cast<void**>(&g_originalChangeRoom))) {
        return false;
    }
    game_tick::addCallback("room_move", onTick);
    return true;
}

void uninstall() { hooks::remove(game::kChangeRoomFunction); }

}  // namespace room_move
