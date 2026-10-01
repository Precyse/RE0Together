#include "event_place.h"

#include <cstring>
#include <mutex>
#include <optional>

#include "character_owner.h"
#include "game.h"
#include "game_state.h"
#include "game_tick.h"
#include "log.h"
#include "net_pad.h"
#include "position_blend.h"
#include "protocol.h"
#include "room_phase.h"
#include "scene.h"

namespace {

using character_owner::Character;
using event_place::CharacterPlace;

// Closer than this the owner's own reports already agree; an event that moved the character further placed it.
constexpr float kMovedDistance = 50.0f;
constexpr uint32_t kSceneEntry = 0;

NetClient* g_net = nullptr;
bool g_inEvent = false;                     // game thread
std::optional<CharacterPlace> g_atEventStart;  // game thread: the peer's character as the event began

std::mutex g_mutex;
std::optional<CharacterPlace> g_incoming;  // guarded by g_mutex

std::optional<CharacterPlace> placeOf(Character character) {
    const uintptr_t player = character_owner::find(character);
    CharacterPlace place{static_cast<uint8_t>(character), 0, scene::of(player), {}, {}};
    if (!player || !game::readTransform(player, place.pos, place.quat)) return std::nullopt;
    return place;
}

bool moved(const CharacterPlace& before, const CharacterPlace& after) {
    return before.scene != after.scene || position_blend::distance(before.pos, after.pos) > kMovedDistance;
}

void watchEvents() {
    const bool inEvent = room_phase::isEvent(game_state::roomPhase());
    if (inEvent == g_inEvent) return;
    g_inEvent = inEvent;
    const Character peer = character_owner::other(character_owner::localCharacter());
    if (inEvent) {
        g_atEventStart = placeOf(peer);
        return;
    }
    const std::optional<CharacterPlace> now = placeOf(peer);
    if (!g_atEventStart || !now || !moved(*g_atEventStart, *now)) return;
    if (g_net->send(proto::kMsgCharacterPlace, true, proto::kSlotAll, proto::bytesOf(*now))) {
        logger::write("event_place: the event moved %s to scene 0x%02x, sent to its owner", character_owner::name(peer),
                      now->scene);
    }
}

// The owner puts its own character where the other machine's event left it.
void applyIncoming() {
    std::optional<CharacterPlace> place;
    {
        std::lock_guard lock(g_mutex);
        place.swap(g_incoming);
    }
    const auto own = character_owner::localCharacter();
    if (!place || static_cast<Character>(place->characterId) != own) return;
    const uintptr_t player = character_owner::find(own);
    if (place->scene != scene::current()) {
        scene::move(player, place->scene, kSceneEntry);
        // The game's own switch to the partner loads that room; camera_parity then puts the camera back on us.
        if (scene::of(game::partner()) == place->scene) game::requestRoomPhase(room_phase::Change);
    }
    game::writeTransform(player, place->pos, place->quat);
    logger::write("event_place: placed %s by the peer's event (scene 0x%02x)", character_owner::name(own), place->scene);
}

void onTick() {
    if (!net_pad::active()) {
        g_inEvent = false;
        return;
    }
    watchEvents();
    if (!game_state::doorActive() && game_state::roomPhase() == room_phase::Main) applyIncoming();
}

}  // namespace

namespace event_place {

void onFrame(const GameFrame& frame) {
    if (frame.type != proto::kMsgCharacterPlace || frame.payload.size() != sizeof(CharacterPlace) ||
        frame.slot != net_pad::peerSlot()) {
        return;
    }
    CharacterPlace place;
    std::memcpy(&place, frame.payload.data(), sizeof(place));
    std::lock_guard lock(g_mutex);
    g_incoming = place;
}

void enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("event_place", onTick);
}

}  // namespace event_place
