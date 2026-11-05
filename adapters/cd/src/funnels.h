#pragma once
// The two client byte funnels (cd design, adapter.md 4): ClientNet::PostPacket carries every Req our client sends to its
// in-process server, ClientNet::OnRecvPacket every Ack that server sends back. Each packet is counted per opcode,
// optionally captured to file, and, for the opcodes a module watches, queued for the main tick (the first payload bytes,
// or the whole payload for opcodes watched in full). Acks for the other players' copies are injected from inside the
// real OnRecvPacket call on the client's network thread, before the real packet (cdmp's route, proven on two PCs):
// at most kInjectPerCall per real packet, each marked so the observer skips it. Nothing real is dropped or rewritten.
#include <array>
#include <cstdint>
#include <vector>

#include "packet.h"

namespace funnels {

constexpr size_t kEventHeadBytes = 32;

constexpr size_t kInjectPerCall = 64;

// A watched packet: its header and the first payload bytes.
struct Event {
    packet::Direction direction = packet::Direction::ClientToServer;
    packet::Header header;
    std::array<uint8_t, kEventHeadBytes> head{};
    std::vector<uint8_t> payload;  // the whole payload, for opcodes watched in full
};

// Before install only: packets with this opcode become events; `full` also copies the whole payload.
void watch(uint16_t opcode, bool full = false);

// Any thread: queues a whole packet (header included) for our own client. Dropped and counted when injection is off
// (a fault in an injected call turns it off for the session) or the queue is full.
void inject(std::vector<uint8_t> packet);
bool injectionOn();

// Any thread: queues one of our own Reqs (header included) for our server. It goes out from inside the game's next
// real PostPacket call, with that call's ClientNet and session; one that waited over 3 s is dropped and counted, as is
// any while posting is off (a fault in a posted call turns it off for the session) or the queue is full.
void post(std::vector<uint8_t> packet);
bool postingOn();

// The opcode a hold applies to (before install). While holding, the game's newest packet of it is kept instead of sent
// (the real call's result is set as when it queued it); ending the hold posts the kept one. Any thread.
void holdOpcode(uint16_t opcode);
void setHolding(bool on);
uint64_t heldCount();

// Installs both hooks as one set (all or nothing, inside the caller's hooks batch); false when the site bytes differ.
bool install(bool capture);

// Main tick: the events queued since the last call, oldest first.
std::vector<Event> takeEvents();

struct Totals {
    uint64_t packets[2] = {};  // by direction
    uint64_t bytes[2] = {};
    uint64_t malformed = 0;     // header length disagrees with the size
    uint64_t eventsDropped = 0;
    uint64_t faults = 0;        // exceptions caught inside our observer
    uint64_t injected = 0;
    uint64_t injectDropped = 0;
    uint64_t injectFaults = 0;
    uint64_t posted = 0;
    uint64_t postDropped = 0;
    uint64_t postFaults = 0;
};
Totals totals();

// Packets seen per opcode in one direction since start, for the counters line.
uint32_t opcodeCount(packet::Direction direction, uint16_t opcode);

}  // namespace funnels
