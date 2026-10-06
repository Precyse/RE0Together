#pragma once
// DS2-internal: what the engine's Sequence objects (cutscene timelines, entity class size 0x640) tell: the resource they
// play, how far they are, which SequenceNetwork owns them. Reads are safe against freed memory; any thread.
#include <cstddef>
#include <cstdint>

namespace sequence_info {

constexpr size_t kUuidSize = 16;

struct Info {
    uint8_t category;  // ESequenceCategory of the resource
    int32_t stopFrame;
    uint8_t resource[kUuidSize];  // the SequenceResource's UUID
    uint8_t entity[kUuidSize];    // the Sequence entity's UUID
    uint8_t network[kUuidSize];   // the owning SequenceNetwork's UUID, all zero when none is found
};

// False when the Sequence does not play a SequenceResource (a network's own root Sequence) or cannot be read.
bool read(uintptr_t sequence, Info& out);

// The raw walk to the resource, for the log when `read` fails.
struct Probe {
    uintptr_t ref, holder, resource, resourceVtable;
    uint64_t flags;
};
Probe probe(uintptr_t sequence);

// Whether the entity is a Sequence (by its vtable).
bool isSequence(uintptr_t entity);

bool started(uintptr_t sequence);
int32_t frame(uintptr_t sequence);

// Whether the Sequence tree this Sequence belongs to already has a stop reason (the first stop wins).
bool stopRecorded(uintptr_t sequence);

// Starts the loaded SequenceNetwork with this resource UUID, the engine's own script call; nothing when none is loaded.
void startNetwork(const uint8_t* uuid);

}  // namespace sequence_info
