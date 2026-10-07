#pragma once
#include <cstdint>

#include "net_client.h"

// Room script events run on both machines (docs/re/RE0_EVENT_SYNC.md, rules in event_rule.h). The machine whose game
// fires a trigger (the firer) runs the thread natively and sends its start and every finished opcode; the peer, when
// it has the same room loaded, starts the same thread and runs each opcode only after the firer finished it, taking the
// firer's branch, with the firer's character as the controlled one. Shared triggers fire on the room's authority only.
namespace event_sync {

// Wire payload of EVENT_START (0x0130), reliable.
struct EventStart {
    uint32_t key;      // the script key the thread runs under (the scene id for room triggers)
    uint16_t scene;    // the firer's loaded scene
    uint16_t serial;   // the firer's id for this thread, named by its steps
    uint16_t index;    // trigger index, game::kForkThreadIndex for a forked thread
    uint16_t pc;       // where to start: the trigger's entry, or the firer's current op for a late arrival
    uint8_t type;      // the trigger's condition type, kForkType for a fork
    uint8_t character; // the firer's own character (character_owner::Character)
    uint8_t kind;      // event_rule::TriggerKind, for the log
    uint8_t reserved;
};
static_assert(sizeof(EventStart) == 16);

constexpr uint8_t kForkType = 0xff;

// One finished opcode of a fired thread; EVENT_STEPS (0x0131, reliable) is a u16 count, u16 pad, then the steps.
struct EventStep {
    uint16_t serial;
    uint16_t from;   // pc of the op
    uint16_t to;     // pc after it
    uint8_t result;  // event_rule::kResult*
    uint8_t reserved;
};
static_assert(sizeof(EventStep) == 8);

constexpr size_t kStepsHeaderSize = 4;

// Net thread: EVENT_START and EVENT_STEPS from the peer.
void onFrame(const GameFrame& frame);

// Hooks the trigger start, the opcode dispatch and the script update.
bool enable(NetClient& net);

void uninstall();

}  // namespace event_sync
