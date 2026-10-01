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
#include "state_correction.h"

namespace {

using character_owner::Character;
using event_place::CharacterPlace;

// Further than this from where its owner says it stands, the peer's character was placed by the cutscene.
constexpr float kMovedDistance = 50.0f;
constexpr uint32_t kSceneEntry = 0;

NetClient* g_net = nullptr;
bool g_inCutscene = false;  // game thread

std::mutex g_mutex;
std::optional<CharacterPlace> g_incoming;  // guarded by g_mutex

std::optional<CharacterPlace> placeOf(Character character) {
    const uintptr_t player = character_owner::find(character);
    CharacterPlace place{static_cast<uint8_t>(character), 0, scene::of(player), {}, {}};
    if (!player || !game::readTransform(player, place.pos, place.quat)) return std::nullopt;
    return place;
}

// Cutscenes hold the peer's world (menu_mirror), so its owner's reports stand still while the cutscene plays here; a
// copy that ends up elsewhere than the owner's own report was placed by the cutscene. (Scripted events that do not
// hold the world are left to the normal position correction: the owner keeps playing through them.)
bool cutscene(int32_t phase) { return room_phase::isEvent(phase) && room_phase::pausesWorld(phase); }

bool placedByCutscene(const CharacterPlace& here) {
    state_sync::PlayerState owner;
    if (!state_correction::latestState(owner) || owner.characterId != here.characterId) return false;
    return owner.room != here.scene || position_blend::distance(owner.pos, here.pos) > kMovedDistance;
}

void watchCutscenes() {
    const bool inCutscene = cutscene(game_state::roomPhase());
    if (inCutscene == g_inCutscene) return;
    g_inCutscene = inCutscene;
    const Character own = character_owner::localCharacter();
    if (inCutscene || own == Character::Unknown) return;
    const Character peer = character_owner::other(own);
    const std::optional<CharacterPlace> now = placeOf(peer);
    if (!now || !placedByCutscene(*now)) return;
    if (g_net->send(proto::kMsgCharacterPlace, true, proto::kSlotAll, proto::bytesOf(*now))) {
        logger::write("event_place: the cutscene moved %s to scene 0x%02x, sent to its owner",
                      character_owner::name(peer), now->scene);
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
        // Only into the room the partner stands in: the game's switch to it loads that room, and camera_parity then
        // puts the camera back on us. Anywhere else the camera would be left on an empty room.
        if (scene::of(game::partner()) != place->scene) {
            logger::write("event_place: placement into scene 0x%02x skipped, no character of ours there", place->scene);
            return;
        }
        scene::move(player, place->scene, kSceneEntry);
        game::requestRoomPhase(room_phase::Change);
    }
    game::writeTransform(player, place->pos, place->quat);
    logger::write("event_place: placed %s by the peer's event (scene 0x%02x)", character_owner::name(own), place->scene);
}

void onTick() {
    if (!net_pad::active()) {
        g_inCutscene = false;
        return;
    }
    watchCutscenes();
    if (game_state::playing()) applyIncoming();
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
