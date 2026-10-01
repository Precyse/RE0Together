#pragma once
#include <cstdint>

#include "net_client.h"

// Cutscenes run on the machine that triggered them, and may move the other player's character (step it aside, put it
// behind a gate, bring it into the room). That character belongs to its owner, whose position reports would pull it
// straight back, so the result is handed to the owner: when a cutscene ends here and the peer's character stands
// elsewhere than its owner's own latest report, CHARACTER_PLACE tells the owner where it now stands, and the owner
// moves its own character there (into another room too, when its partner is there: scene::move and the game's switch).
namespace event_place {

// Wire payload of CHARACTER_PLACE (0x0113), reliable, to the owner.
struct CharacterPlace {
    uint8_t characterId;
    uint8_t reserved;
    uint16_t scene;
    float pos[3];
    float quat[4];
};
static_assert(sizeof(CharacterPlace) == 32);

// Net thread: queues a placement of this machine's own character.
void onFrame(const GameFrame& frame);

// Registers the per-frame event watch and apply.
void enable(NetClient& net);

}  // namespace event_place
