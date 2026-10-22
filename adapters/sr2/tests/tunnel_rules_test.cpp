// The pure rules of the Steam tunnel (no game): virtual addresses, the collision rule, routing, the frame codec, inbound
// acceptance, the inbox and the counters line.
#include <cstdio>
#include <vector>

#include "../src/tunnel_rules.h"
#include "check.h"

namespace {

using namespace tunnel_rules;

constexpr uint64_t kSteamBase = 0x0110000100000000ull;  // the universe and account type bits of a user's SteamID64
constexpr uint8_t kHostSlot = 0;
constexpr uint8_t kGuestSlot = 2;
constexpr uint32_t kOwn = 0x0A000101;
constexpr uint64_t kSweep = 70000;
constexpr uint32_t kLastOctet = 0xFF;

std::vector<uint8_t> bytesOfSize(size_t size) { return std::vector<uint8_t>(size, 0x5A); }

}  // namespace

int main() {
    // Virtual addresses.
    check(virtualAddress(kSteamBase | 0x00012345) == 0x0A012346, "10.a.b.c from the account id: 0x012345 is 10.1.35.70");
    check(virtualAddress(kSteamBase | 0x00FF0000) == 0x0AFF0001, "a zero low byte gives last octet 1");
    check(virtualAddress(kSteamBase | 0x000000FD) == 0x0A0000FE, "low byte 253 gives last octet 254");
    check(virtualAddress(kSteamBase | 0x000000FE) == 0x0A000001 && virtualAddress(kSteamBase | 0x000000FF) == 0x0A000002,
          "low bytes 254 and 255 wrap to 1 and 2");
    bool neverEdge = true;
    for (uint64_t account = 0; account < kSweep; ++account) {
        const uint32_t address = virtualAddress(kSteamBase | account);
        const uint32_t last = address & kLastOctet;
        if (last == 0 || last == kLastOctet || !isVirtual(address)) neverEdge = false;
    }
    check(neverEdge, "the last octet is never 0 or 255, and every address is in 10.0.0.0/8");
    check(virtualAddress(kSteamBase | 0xABCDEF) == virtualAddress(0x0000000000ABCDEFull) &&
              virtualAddress(kSteamBase | 0xABCDEF) == virtualAddress(kSteamBase | 0xABCDEF),
          "deterministic, and only the account id (low 32 bits) counts");
    check(formatAddress(0x0A012346) == "10.1.35.70" && formatAddress(kBroadcast) == "255.255.255.255", "addresses print as dotted quads");

    // The collision rule: accounts that differ only by 254 in the low byte share an address.
    const uint64_t low = kSteamBase | 0x00012300;
    const uint64_t high = kSteamBase | 0x000123FE;
    check(virtualAddress(low) == virtualAddress(high), "two accounts can share an address");
    const Roster clash = mapMembers({{high, 2}, {low, 1}, {kSteamBase | 0x0000AA01, 3}});
    check(clash.members.size() == 2 && clash.collisions.size() == 1, "one of two members with one address is unmapped");
    check(clash.collisions[0].kept == low && clash.collisions[0].dropped == high && clash.collisions[0].address == virtualAddress(low),
          "the higher SteamID is the one left out, whatever order the members come in");
    const Roster again = mapMembers({{low, 1}, {kSteamBase | 0x0000AA01, 3}, {high, 2}});
    check(again.collisions.size() == 1 && again.collisions[0].dropped == high, "every machine decides alike");
    check(mapMembers({}).members.empty() && mapMembers({{low, 1}}).collisions.empty(), "no members, no collisions");

    // Routing.
    const std::vector<Mapped> peers = {{kSteamBase | 0x00000A01, kHostSlot, 0x0A000A02},
                                       {kSteamBase | 0x00000B01, kGuestSlot, 0x0A000B02}};
    const Routing hostBroadcast = classify(kBroadcast, true, kHostSlot, peers);
    check(hostBroadcast.route == Route::Broadcast && hostBroadcast.slot == kSlotAll, "a host's broadcast goes to every peer (slot 0xFF)");
    const Routing guestBroadcast = classify(kBroadcast, false, kHostSlot, peers);
    check(guestBroadcast.route == Route::BroadcastToHost && guestBroadcast.slot == kHostSlot, "a guest's broadcast goes to the host only");
    const Routing unicast = classify(0x0A000B02, true, kHostSlot, peers);
    check(unicast.route == Route::Peer && unicast.slot == kGuestSlot, "a peer's address goes to that peer's current slot");
    check(classify(0x0A000C03, true, kHostSlot, peers).route == Route::NoPeer, "a virtual address nobody has: no peer");
    check(classify(0xC0A80101, true, kHostSlot, peers).route == Route::NotVirtual &&
              classify(0x7F000001, false, kHostSlot, peers).route == Route::NotVirtual &&
              classify(0, true, kHostSlot, {}).route == Route::NotVirtual,
          "any other address is not virtual");
    check(classify(0x0A000B02, true, kHostSlot, {}).route == Route::NoPeer, "without peers every virtual address has no peer");

    // The frame codec.
    const auto frame = encode(0x0A000B02, bytesOfSize(20));
    check(frame && frame->size() == kDestinationBytes + 20 && (*frame)[0] == 0x02 && (*frame)[1] == 0x0B && (*frame)[2] == 0x00 &&
              (*frame)[3] == 0x0A,
          "a frame is the destination, little endian, then the datagram");
    const auto back = decode(*frame);
    check(back && back->destination == 0x0A000B02 && back->datagram.size() == 20 && back->datagram[0] == 0x5A, "it decodes to the same");
    check(encode(kBroadcast, bytesOfSize(1)) && encode(kBroadcast, bytesOfSize(kMaxDatagram)), "1 and 1300 bytes are sent");
    check(!encode(kBroadcast, bytesOfSize(0)) && !encode(kBroadcast, bytesOfSize(kMaxDatagram + 1)), "0 and 1301 bytes are refused");
    check(kMaxFramePayload == 1304 && decode(bytesOfSize(kMaxFramePayload)) && !decode(bytesOfSize(kMaxFramePayload + 1)),
          "a frame payload may be 1304 bytes, not 1305");
    check(decode(bytesOfSize(kDestinationBytes + 1)) && !decode(bytesOfSize(kDestinationBytes)) && !decode({}),
          "a frame payload needs at least 5 bytes");

    // Inbound.
    const InboundResult toOwn = acceptInbound(kOwn, kOwn, kGuestSlot, peers);
    check(toOwn.verdict == Inbound::Accept && toOwn.source == 0x0A000B02, "a datagram to us from a member is accepted, with its source address");
    check(acceptInbound(kBroadcast, kOwn, kHostSlot, peers).verdict == Inbound::Accept, "a broadcast is accepted");
    check(acceptInbound(0x0A000909, kOwn, kHostSlot, peers).verdict == Inbound::WrongDestination, "another address is not ours");
    check(acceptInbound(kOwn, kOwn, 7, peers).verdict == Inbound::NoPeer && acceptInbound(kBroadcast, kOwn, 7, {}).verdict == Inbound::NoPeer,
          "a source slot without an address is refused");
    check(acceptInbound(0x0A000909, kOwn, 7, peers).verdict == Inbound::WrongDestination, "the destination is judged first");

    // The inbox.
    Inbox<int> inbox;
    check(inbox.empty() && !inbox.pop(), "an empty inbox has nothing");
    bool droppedWhileFilling = false;
    for (size_t i = 0; i < kInboxCapacity; ++i) droppedWhileFilling = inbox.push(static_cast<int>(i)) || droppedWhileFilling;
    check(!droppedWhileFilling, "256 datagrams fit");
    check(inbox.push(-1), "the 257th drops the oldest");
    check(*inbox.pop() == 1, "so the first datagram is gone and the second is next");
    size_t left = 1;
    while (inbox.pop()) ++left;
    check(left == kInboxCapacity, "256 datagrams are kept");

    // Counters.
    Counters counters;
    ++counters.sent;
    counters.received += 2;
    ++counters.large;
    const CounterValues values = counters.values();
    check(values.sent == 1 && values.received == 2 && values.large == 1 && values.noPeer == 0, "the counters count");
    check(describe(values) == "sent 1 received 2 dropped no_peer 0 not_virtual 0 wrong_dst 0 inbox_full 0 oversize 0, large 1",
          "the log line names every counter");
    check(values == counters.values() && !(values == CounterValues{}), "values compare");
    check(describeSlots(counters).empty(), "no peer line before any peer traffic");
    counters.countSentTo(1);
    counters.countSentTo(1);
    counters.countReceivedFrom(2);
    counters.countSentTo(kSlotAll);
    counters.countReceivedFrom(static_cast<uint8_t>(kCountedSlots));
    check(describeSlots(counters) == "slot 1 sent 2 received 0, slot 2 sent 0 received 1",
          "per peer: datagrams to and from each slot, a broadcast and an out of range slot not counted");
    return g_failures == 0 ? 0 : 1;
}
