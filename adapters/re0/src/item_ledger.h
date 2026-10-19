#pragma once
#include <cstdint>

#include "item_ledger_rule.h"

// Host item ledger for floor takes (partner_mode=state, docs/RE0_NOTES.md "State mode: item ledger"). Every pickup asks
// the host before it takes: the host grants an item to the first request and refuses the rest, so two players reaching
// for one herb leave with one. The host's own pickups go through the same ledger code locally. pickup_guard holds the
// engine's take step while the answer is out; floor_items_sync tells the ledger about puts and takes.
namespace item_ledger {

using item_ledger_rule::Key;
using item_ledger_rule::Vec3;

// The floor item a pickup is about to take.
struct Take {
    uint16_t scene;  // stage << 8 | room
    uint32_t itemId;
    uint32_t count;
    Vec3 pos;
};

enum class Verdict : uint8_t {
    Run,   // granted (or nobody to ask): the engine's step runs
    Hold,  // the host has not answered: the step does not run
    End    // refused: the pickup ends without the item
};

struct Resolution {
    Verdict verdict;
    bool requested;  // this call sent the request to the host
    item_ledger_rule::Answer answer;  // the refusal's reason, when verdict is End
};

// Registers TAKE_REQUEST (guest to host) and TAKE_RESULT (host to all) with the frame router. False when a type could
// not be registered.
bool enable();

// Game thread, every frame of a pickup's take phase: the host decides at once, a guest sends the request on the first
// call and then holds until the answer comes, or ends the pickup when none does within a couple of seconds. Called with
// nobody linked, the take runs.
Resolution resolveTake(const Take& take);

// Game thread: a new pickup begins; the previous one's answer no longer applies.
void resetTake();

// An item was put on the floor, by anyone and from any source: whatever the ledger holds for that spot is gone with it.
// Host only (ignored elsewhere). Any thread.
void notePut(const Key& key);

// An item left the floor, taken by anyone (own take or a FLOOR_TAKE): it is consumed. Host only. Any thread.
void noteTaken(const Key& key);

// A save or a load makes the game's own data current: forget the journal. Any thread.
void clear();

}  // namespace item_ledger
