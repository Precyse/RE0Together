#pragma once
#include <cstdint>
#include <optional>

#include "character_owner.h"
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

// partner_mode=state: a script op that ran here wrote the copy of the peer's character (SubMove for an owner that does
// not run the thread, climb type 7), which its owner's stream would only overwrite. `mark` is taken before the op and
// `reportMoved` after it: when the copy moved more than 50 units or into another room, CHARACTER_PLACE tells the owner.
struct Mark {
    character_owner::Character peer;
    uint16_t scene;
    float pos[3];
};

std::optional<Mark> mark(character_owner::Character peer);
void reportMoved(const Mark& before);

// Net thread: queues a placement of this machine's own character.
void onFrame(const GameFrame& frame);

// Registers the per-frame event watch and apply.
void enable(NetClient& net);

}  // namespace event_place
