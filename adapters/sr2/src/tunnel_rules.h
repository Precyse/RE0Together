#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The pure rules of the Steam tunnel (unit tested; docs/SR2_NOTES.md "Steam tunnel"): the game's UDP datagrams travel as
// GAME_DATAGRAM frames between the adapters. Addresses are host-order IPv4 (10.1.2.3 = 0x0A010203).
namespace tunnel_rules {

constexpr uint16_t kGameDatagramType = 0x0100;  // the adapter's own message type, unreliable
constexpr uint8_t kSlotAll = 0xFF;              // the launcher's "every peer" destination
constexpr uint32_t kBroadcast = 0xFFFFFFFF;
constexpr uint32_t kSubnet = 0x0A000000;        // 10.0.0.0/8, the virtual subnet
constexpr uint32_t kSubnetMask = 0xFF000000;
constexpr uint32_t kUsableLastOctets = 254;     // 1..254: the last octet is never 0 or 255
constexpr unsigned kByteBits = 8;

constexpr size_t kMaxDatagram = 1300;  // psnet refuses more (0x514)
constexpr size_t kDestinationBytes = 4;
constexpr size_t kMaxFramePayload = kDestinationBytes + kMaxDatagram;
constexpr size_t kLargeDatagram = 1150;  // Steam fragments unreliable messages above about 1200 bytes
constexpr size_t kInboxCapacity = 256;
constexpr size_t kCountedSlots = 4;      // the session's player slots 0..3, counted per peer with trace=1

// A SteamID64 as a virtual address: 10.a.b.c from the account id (its low 32 bits).
constexpr uint32_t virtualAddress(uint64_t steamId) {
    const uint32_t account = static_cast<uint32_t>(steamId);
    const uint32_t a = (account >> (2 * kByteBits)) & 0xFF;
    const uint32_t b = (account >> kByteBits) & 0xFF;
    const uint32_t c = 1 + (account & 0xFF) % kUsableLastOctets;
    return kSubnet | a << (2 * kByteBits) | b << kByteBits | c;
}

constexpr bool isVirtual(uint32_t address) { return (address & kSubnetMask) == kSubnet; }

inline std::string formatAddress(uint32_t address) {
    char text[16];
    snprintf(text, sizeof(text), "%u.%u.%u.%u", address >> 24, (address >> 16) & 0xFF, (address >> 8) & 0xFF, address & 0xFF);
    return text;
}

// A session member as the launcher reports it, and as the tunnel knows it.
struct Member {
    uint64_t steamId;
    uint8_t slot;
};
struct Mapped {
    uint64_t steamId;
    uint8_t slot;
    uint32_t address;

    bool operator==(const Mapped&) const = default;
};
struct Collision {
    uint64_t kept;
    uint64_t dropped;
    uint32_t address;
};
struct Roster {
    std::vector<Mapped> members;
    std::vector<Collision> collisions;
};

// Every member with its virtual address. Two members with one address leave the higher SteamID unmapped; all machines see
// the same members, so all decide alike.
inline Roster mapMembers(std::vector<Member> members) {
    std::sort(members.begin(), members.end(), [](const Member& a, const Member& b) { return a.steamId < b.steamId; });
    Roster roster;
    for (const Member& member : members) {
        const uint32_t address = virtualAddress(member.steamId);
        const auto holder = std::find_if(roster.members.begin(), roster.members.end(),
                                         [&](const Mapped& mapped) { return mapped.address == address; });
        if (holder != roster.members.end()) {
            roster.collisions.push_back({holder->steamId, member.steamId, address});
        } else {
            roster.members.push_back({member.steamId, member.slot, address});
        }
    }
    return roster;
}

// Where a datagram the game sends to `destination` goes.
enum class Route { Broadcast, BroadcastToHost, Peer, NoPeer, NotVirtual };

struct Routing {
    Route route;
    uint8_t slot;  // Broadcast: every peer, BroadcastToHost: the host's, Peer: that peer's current slot
};

// A broadcast from the host goes to every peer, from a guest to the host only (only ping and pong are broadcast, and a
// guest's ping is for the host). `peers` are the other members.
inline Routing classify(uint32_t destination, bool isHost, uint8_t hostSlot, std::span<const Mapped> peers) {
    if (destination == kBroadcast) return isHost ? Routing{Route::Broadcast, kSlotAll} : Routing{Route::BroadcastToHost, hostSlot};
    for (const Mapped& peer : peers) {
        if (peer.address == destination) return {Route::Peer, peer.slot};
    }
    return {isVirtual(destination) ? Route::NoPeer : Route::NotVirtual, kSlotAll};
}

// The frame payload of a datagram: the destination (little endian), then the datagram. None when it is empty or longer
// than the game can send.
inline std::optional<std::vector<uint8_t>> encode(uint32_t destination, std::span<const uint8_t> datagram) {
    if (datagram.empty() || datagram.size() > kMaxDatagram) return std::nullopt;
    std::vector<uint8_t> payload(kDestinationBytes + datagram.size());
    memcpy(payload.data(), &destination, kDestinationBytes);
    memcpy(payload.data() + kDestinationBytes, datagram.data(), datagram.size());
    return payload;
}

struct Decoded {
    uint32_t destination;
    std::span<const uint8_t> datagram;
};

// None unless the payload is a destination and 1..kMaxDatagram bytes.
inline std::optional<Decoded> decode(std::span<const uint8_t> payload) {
    if (payload.size() <= kDestinationBytes || payload.size() > kMaxFramePayload) return std::nullopt;
    uint32_t destination = 0;
    memcpy(&destination, payload.data(), kDestinationBytes);
    return Decoded{destination, payload.subspan(kDestinationBytes)};
}

enum class Inbound { Accept, WrongDestination, NoPeer };

struct InboundResult {
    Inbound verdict;
    uint32_t source;  // the sender's virtual address, when accepted
};

// A datagram is for us when it is addressed to our address or broadcast, and from a member that has an address.
inline InboundResult acceptInbound(uint32_t destination, uint32_t own, uint8_t sourceSlot, std::span<const Mapped> peers) {
    if (destination != own && destination != kBroadcast) return {Inbound::WrongDestination, 0};
    for (const Mapped& peer : peers) {
        if (peer.slot == sourceSlot) return {Inbound::Accept, peer.address};
    }
    return {Inbound::NoPeer, 0};
}

// Datagrams that have arrived and wait for the game. When full, the oldest is dropped.
template <class T>
class Inbox {
public:
    // True when the oldest datagram was dropped to make room.
    bool push(T item) {
        const bool full = items_.size() >= kInboxCapacity;
        if (full) items_.pop_front();
        items_.push_back(std::move(item));
        return full;
    }
    std::optional<T> pop() {
        if (items_.empty()) return std::nullopt;
        T item = std::move(items_.front());
        items_.pop_front();
        return item;
    }
    bool empty() const { return items_.empty(); }

private:
    std::deque<T> items_;
};

struct CounterValues {
    uint64_t sent = 0;
    uint64_t received = 0;
    uint64_t noPeer = 0;
    uint64_t notVirtual = 0;
    uint64_t wrongDestination = 0;
    uint64_t inboxFull = 0;
    uint64_t oversize = 0;
    uint64_t large = 0;

    bool operator==(const CounterValues&) const = default;
};

// The counters, bumped from several threads. `sentTo` counts the datagrams addressed to one peer (a broadcast is not a peer's),
// `receivedFrom` the accepted datagrams by the sender's slot.
class Counters {
public:
    std::atomic<uint64_t> sent{0}, received{0}, noPeer{0}, notVirtual{0}, wrongDestination{0}, inboxFull{0}, oversize{0}, large{0};
    std::atomic<uint64_t> sentTo[kCountedSlots]{}, receivedFrom[kCountedSlots]{};

    void countSentTo(uint8_t slot) {
        if (slot < kCountedSlots) ++sentTo[slot];
    }
    void countReceivedFrom(uint8_t slot) {
        if (slot < kCountedSlots) ++receivedFrom[slot];
    }

    CounterValues values() const {
        return {sent, received, noPeer, notVirtual, wrongDestination, inboxFull, oversize, large};
    }
};

inline std::string describe(const CounterValues& values) {
    char text[192];
    snprintf(text, sizeof(text),
             "sent %llu received %llu dropped no_peer %llu not_virtual %llu wrong_dst %llu inbox_full %llu oversize %llu, large %llu",
             values.sent, values.received, values.noPeer, values.notVirtual, values.wrongDestination, values.inboxFull,
             values.oversize, values.large);
    return text;
}

// "slot 1 sent S received R, slot 2 ..." for every slot that has traffic.
inline std::string describeSlots(const Counters& counters) {
    std::string text;
    for (size_t slot = 0; slot < kCountedSlots; ++slot) {
        const uint64_t sent = counters.sentTo[slot];
        const uint64_t received = counters.receivedFrom[slot];
        if (sent == 0 && received == 0) continue;
        char part[64];
        snprintf(part, sizeof(part), "%sslot %zu sent %llu received %llu", text.empty() ? "" : ", ", slot, static_cast<unsigned long long>(sent),
                 static_cast<unsigned long long>(received));
        text += part;
    }
    return text;
}

}  // namespace tunnel_rules
