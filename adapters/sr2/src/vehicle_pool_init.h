#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "vehicle_pool_rules.h"

// The stores that bring a new vehicle table to the state the game's static constructors and resets leave the shipped one in
// (milestone 15 B3, unit tested on plain memory; docs/SR2_NOTES.md, "Vehicle pool (milestone 15)"). Every address is the
// address of memory the adapter owns; the game's own constructors it must call are passed in.
namespace vehicle_pool_init {

constexpr uint32_t kMinusOne = 0xFFFFFFFF;

inline uint32_t load(uintptr_t at) { return *reinterpret_cast<const uint32_t*>(at); }
inline void store(uintptr_t at, uint32_t value) { *reinterpret_cast<uint32_t*>(at) = value; }

// The game's constructors the new tables need; each takes the address of one entry (or, for the vector, of its first entry).
struct Constructors {
    void (*vehicle)(uintptr_t object);        // 0xAE1300
    void (*mempool)(uintptr_t object);        // 0xBF48F0
    void (*sixteenByteVector)(uintptr_t first);  // 0x402450(first, 0x10, 8, 0xAE5720), stdcall
};

// T1's element constructor (0xDB2160): the marker bytes, the -1 links and the vector of eight 16-byte records at +0x3C.
constexpr uint32_t kT1Marker = 0xFF;
constexpr uint32_t kT1MarkerOffsets[] = {0xC, 0x18, 0x30};
constexpr uint32_t kT1LinkOffsets[] = {0x10, 0x14, 0x1C, 0x20, 0x34, 0x38, 0x11C, 0x120};
constexpr uint32_t kT1VectorOffset = 0x3C;

inline void constructT1Element(uintptr_t element, const Constructors& game) {
    for (const uint32_t offset : kT1MarkerOffsets) store(element + offset, kT1Marker);
    for (const uint32_t offset : kT1LinkOffsets) store(element + offset, kMinusOne);
    game.sixteenByteVector(element + kT1VectorOffset);
}

// T2's element constructor (0xDB21D0): a self link at +0x14 through +8, -1 at +4, a flag word at +0x10.
constexpr uint32_t kT2SelfLinkOffset = 0x14;
constexpr uint32_t kT2Flags = 0x80000008;

inline void constructT2Element(uintptr_t element) {
    store(element + 4, kMinusOne);
    store(element + 8, static_cast<uint32_t>(element + kT2SelfLinkOffset));
    store(element + 0xC, 0);
    store(element + 0x10, kT2Flags);
}

// What the constructors the game ran at start-up do to the new arrays (the memory is zero, as .bss is): the vehicle objects, the
// T1 and T2 elements and the customisation mempool objects. The other tables stay zero.
inline void construct(vehicle_pool_sites::TableId id, uintptr_t block, const Constructors& game) {
    using vehicle_pool_sites::TableId;
    const vehicle_pool_sites::Table& table = vehicle_pool_sites::tableOf(id);
    for (uint32_t i = 0; i < vehicle_pool_sites::kGrownEntries; ++i) {
        const uintptr_t entry = block + i * table.elementSize;
        switch (id) {
            case TableId::Pool: game.vehicle(entry); break;
            case TableId::T1: constructT1Element(entry, game); break;
            case TableId::T2: constructT2Element(entry); break;
            case TableId::ObjectsCb:
            case TableId::ObjectsExt: game.mempool(entry); break;
            default: return;
        }
    }
}

// A customisation instance element (0x84 bytes) as the allocator's activation (0xACA8C0) leaves it: four index words are -1, the
// rest zero.
constexpr uint32_t kCustomInstanceSize = 0x84;
constexpr uint32_t kCustomInstanceIndexOffsets[] = {0x30, 0x40, 0x50, 0x5C};

inline void markCustomInstance(uintptr_t element) {
    for (const uint32_t offset : kCustomInstanceIndexOffsets) store(element + offset, kMinusOne);
}

// The world reset's stores for a byte table (0xAE2120): the count byte is 0 and the stack holds the indices 0..entries-1.
inline void resetByteTable(uintptr_t countByte, uint32_t entries) {
    *reinterpret_cast<uint8_t*>(countByte) = 0;
    uint8_t* stack = reinterpret_cast<uint8_t*>(countByte) + vehicle_pool_sites::kCountByteBytes;
    for (uint32_t i = 0; i < entries; ++i) stack[i] = static_cast<uint8_t>(i);
}

// A circular doubly linked free list whose head is the last element put in (the game's way for the vehicle pool and the
// customisation elements): `element` goes between the head's predecessor and the head, and becomes the head.
inline void pushFree(uint32_t& head, uintptr_t element, uint32_t nextOffset, uint32_t previousOffset) {
    if (head == 0) {
        store(element + nextOffset, static_cast<uint32_t>(element));
        store(element + previousOffset, static_cast<uint32_t>(element));
    } else {
        const uintptr_t first = head;
        const uint32_t last = load(first + previousOffset);
        store(last + nextOffset, static_cast<uint32_t>(element));
        store(element + previousOffset, last);
        store(first + previousOffset, static_cast<uint32_t>(element));
        store(element + nextOffset, static_cast<uint32_t>(first));
    }
    head = static_cast<uint32_t>(element);
}

// The category node free list of the vehicles (0x9291C0, type 1): the nodes are appended in order, so the head stays the first
// node, each node's next is the head and its previous the old last node; the count is the number of nodes linked.
constexpr uint32_t kNodeNextOffset = 0;
constexpr uint32_t kNodePreviousOffset = 4;
constexpr uint32_t kNodeSize = 0x1C;

inline void linkNodes(uint32_t& head, uint32_t& freeCount, uintptr_t first, uint32_t entries) {
    head = 0;
    freeCount = 0;
    for (uint32_t i = 0; i < entries; ++i) {
        const uintptr_t node = first + i * kNodeSize;
        if (head == 0) {
            store(node + kNodeNextOffset, static_cast<uint32_t>(node));
            store(node + kNodePreviousOffset, static_cast<uint32_t>(node));
            head = static_cast<uint32_t>(node);
        } else {
            const uint32_t last = load(head + kNodePreviousOffset);
            store(node + kNodeNextOffset, head);
            store(node + kNodePreviousOffset, last);
            store(last + kNodeNextOffset, static_cast<uint32_t>(node));
            store(head + kNodePreviousOffset, static_cast<uint32_t>(node));
        }
        ++freeCount;
    }
}

// The customisation slot loop of 0xAC5A30 for one vehicle: the name and fields of its two mempool objects, whose buffers are
// the vehicle's share of the two buffer blocks. A mempool object is 0x54 bytes: name at +0xC (0x20 bytes), buffer at +0x38,
// its size at +0x3C, +0x40 = 1, the limit and capacity at +0x4C and +0x50.
constexpr uint32_t kMempoolNameOffset = 0xC;
constexpr uint32_t kMempoolNameBytes = 0x20;
constexpr uint32_t kMempoolBufferOffset = 0x38;
constexpr uint32_t kMempoolSizeOffset = 0x3C;
constexpr uint32_t kMempoolOneOffset = 0x40;
constexpr uint32_t kMempoolLimitOffset = 0x4C;
constexpr uint32_t kMempoolCapacityOffset = 0x50;
constexpr uint32_t kCbBufferBytes = 0x1800;
constexpr uint32_t kExtBufferBytes = 0x1400;

inline void resetMempool(uintptr_t object, const char* format, uint32_t index, uint32_t buffer, uint32_t bufferBytes) {
    memset(reinterpret_cast<void*>(object + kMempoolNameOffset), 0, kMempoolNameBytes);
    std::snprintf(reinterpret_cast<char*>(object + kMempoolNameOffset), kMempoolNameBytes, format, index);
    store(object + kMempoolBufferOffset, buffer);
    store(object + kMempoolSizeOffset, bufferBytes);
    store(object + kMempoolOneOffset, 1);
    store(object + kMempoolLimitOffset, bufferBytes);
    store(object + kMempoolCapacityOffset, bufferBytes);
}

}  // namespace vehicle_pool_init
