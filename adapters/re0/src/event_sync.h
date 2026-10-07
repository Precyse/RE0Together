#pragma once
#include <cstdint>

#include "net_client.h"

// Room script events run on both machines (docs/re/RE0_EVENT_SYNC.md, rules in event_rule.h). The thread's leader (the
// owner of the character the script treats as the player: the firer's, until the script switches characters) runs it
// natively and sends its start and every finished opcode; the peer, when it has the same room loaded, starts the same
// thread and runs each opcode only after the leader finished it, taking the leader's branch, with that character as the
// controlled one. Shared triggers fire on the room's authority only.
namespace event_sync {

// A thread's serial is allocated by the machine that fired it; after a lead change the other machine sends steps
// under it, so every message says whose serial it is.
constexpr uint8_t kSerialIsReceivers = 1;  // the serial was allocated by the receiver of this message
constexpr uint8_t kPartnerFollows = 2;     // EVENT_START: the thread's partner still follows (no TraceOff yet)

// Wire payload of EVENT_START (0x0130), reliable.
struct EventStart {
    uint32_t key;      // the script key the thread runs under (the scene id for room triggers)
    uint16_t scene;    // the leader's loaded scene
    uint16_t serial;   // the thread's id, named by its steps
    uint16_t index;    // trigger index, game::kForkThreadIndex for a forked thread
    uint16_t pc;       // where to start: the trigger's entry, or the leader's current op for a late arrival
    uint8_t type;      // the trigger's condition type, kForkType for a fork
    uint8_t character; // the leader's own character, the thread's subject (character_owner::Character)
    uint8_t kind;      // event_rule::TriggerKind, for the log
    uint8_t flags;     // kSerialIsReceivers | kPartnerFollows
};
static_assert(sizeof(EventStart) == 16);

constexpr uint8_t kForkType = 0xff;

// One finished opcode of a led thread; EVENT_STEPS (0x0131, reliable) is a u16 count, u16 pad, then the steps.
struct EventStep {
    uint16_t serial;
    uint16_t from;   // pc of the op
    uint16_t to;     // pc after it
    uint8_t result;  // event_rule::kResult*
    uint8_t flags;   // kSerialIsReceivers
};
static_assert(sizeof(EventStep) == 8);

constexpr size_t kStepsHeaderSize = 4;

// Net thread: EVENT_START and EVENT_STEPS from the peer.
void onFrame(const GameFrame& frame);

// Hooks the trigger start, the opcode dispatch and the script update.
bool enable(NetClient& net);

void uninstall();

}  // namespace event_sync
