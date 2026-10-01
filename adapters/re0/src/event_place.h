#pragma once
#include <cstdint>

#include "net_client.h"

// Cutscenes and scripted events run on the machine that triggered them, and may move the other player's character
// (step it aside, put it behind a gate, bring it into the room). That character belongs to its owner, whose position
// reports would pull it straight back, so the event's result is handed to the owner: when an event ends here and the
// peer's character was moved by it, CHARACTER_PLACE tells the owner where it now stands, and the owner moves its own
// character there (into another room too, through scene::move and the game's own switch).
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
