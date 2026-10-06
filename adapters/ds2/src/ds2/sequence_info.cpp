#include "ds2/sequence_info.h"

#include <algorithm>
#include <array>

#include "decima/safe_read.h"
#include "ds2/engine.h"

namespace {

constexpr uintptr_t kSequenceVtable = 0x14314A378;
constexpr uint8_t kMaxCategory = 7;  // ESequenceCategory: anything above is not a SequenceResource
constexpr uintptr_t kNetworkManagerGlobal = 0x14623E000;  // instance count (int) at +0, array of instance pointers at +8
constexpr uintptr_t kStartNetworkByUuid = 0x14049DF10;    // (const GGUUID*): SequenceNetworkInstance_sExportedStartSequenceNetworkWithUUID
constexpr uintptr_t kManagerCount = 0x0, kManagerArray = 0x8;
constexpr uintptr_t kInstanceResource = 0x58;       // the SequenceNetworkResource
constexpr uintptr_t kInstanceRootSequence = 0x148;  // the network's main Sequence, as a pointer to its secondary base
constexpr uintptr_t kSecondaryBase = 0x20;          // a Sequence referred to by another object is pointed at +0x20
constexpr uintptr_t kObjectUuid = 0x10;
constexpr uintptr_t kResourceRef = 0x68;  // the Sequence's StreamingRef to its resource
constexpr uintptr_t kRefLoadedFlags = 0x8;
constexpr unsigned kRefLoadedBit = 59;
constexpr uintptr_t kHolderObject = 0x20;
constexpr uintptr_t kResourceGameState = 0xC2, kResourceCategory = 0xC3;
constexpr uintptr_t kStarted = 0x341, kStopReason = 0x344, kFrame = 0x34C, kStopFrame = 0x33C, kParent = 0x468;
constexpr int kMaxTreeDepth = 16;
constexpr int32_t kMaxInstances = 4096;

// The resource the Sequence plays, found the way the engine's own start does (the ref's holder, when loaded).
sequence_info::Probe walkToResource(uintptr_t sequence) {
    sequence_info::Probe p{};
    p.ref = decima::readPointer(sequence + kResourceRef);
    if (!p.ref || !decima::safeRead(p.ref + kRefLoadedFlags, p.flags)) return p;
    p.holder = decima::readPointer(p.ref);
    if (p.holder && ((p.flags >> kRefLoadedBit) & 1) != 0) p.resource = decima::readPointer(p.holder + kHolderObject);
    if (p.resource) p.resourceVtable = decima::readPointer(p.resource);
    return p;
}

// The topmost Sequence of the tree this one was made in (a Sequence points at its parent's secondary base).
uintptr_t rootOf(uintptr_t sequence) {
    for (int depth = 0; depth < kMaxTreeDepth; ++depth) {
        const uintptr_t parent = decima::readPointer(sequence + kParent);
        if (!parent) return sequence;
        sequence = parent - kSecondaryBase;
    }
    return 0;
}

bool networkUuidOf(uintptr_t root, uint8_t* out) {
    const uintptr_t manager = decima::readPointer(ds2::at(kNetworkManagerGlobal));
    int32_t count = 0;
    if (!manager || !decima::safeRead(manager + kManagerCount, count) || count <= 0 || count > kMaxInstances) return false;
    const uintptr_t instances = decima::readPointer(manager + kManagerArray);
    for (int32_t i = 0; instances && i < count; ++i) {
        const uintptr_t instance = decima::readPointer(instances + i * sizeof(uintptr_t));
        const uintptr_t main = instance ? decima::readPointer(instance + kInstanceRootSequence) : 0;
        if (!main || main - kSecondaryBase != root) continue;
        const uintptr_t resource = decima::readPointer(instance + kInstanceResource);
        return resource && decima::safeCopy(out, resource + kObjectUuid, sequence_info::kUuidSize);
    }
    return false;
}

}  // namespace

namespace sequence_info {

bool read(uintptr_t sequence, Info& out) {
    const uintptr_t resource = walkToResource(sequence).resource;
    Info info{};
    if (!resource || !decima::safeRead(resource + kResourceGameState, info.gameState) ||
        !decima::safeRead(resource + kResourceCategory, info.category) || info.category > kMaxCategory ||
        !decima::safeRead(sequence + kStopFrame, info.stopFrame) ||
        !decima::safeCopy(info.resource, resource + kObjectUuid, kUuidSize) ||
        !decima::safeCopy(info.entity, sequence + kObjectUuid, kUuidSize)) {
        return false;
    }
    if (info.stopFrame <= 0) return false;
    networkUuidOf(rootOf(sequence), info.network);
    out = info;
    return true;
}

Probe probe(uintptr_t sequence) { return walkToResource(sequence); }

bool isSequence(uintptr_t entity) { return decima::readPointer(entity) == ds2::at(kSequenceVtable); }

bool started(uintptr_t sequence) {
    uint8_t flag = 0;
    return decima::safeRead(sequence + kStarted, flag) && flag != 0;
}

int32_t frame(uintptr_t sequence) {
    int32_t value = 0;
    decima::safeRead(sequence + kFrame, value);
    return value;
}

bool stopRecorded(uintptr_t sequence) {
    const uintptr_t root = rootOf(sequence);
    uint32_t reason = 0;
    return root && decima::safeRead(root + kStopReason, reason) && reason != 0;
}

std::vector<LoadedNetwork> loadedNetworks() {
    std::vector<LoadedNetwork> out;
    const uintptr_t manager = decima::readPointer(ds2::at(kNetworkManagerGlobal));
    int32_t count = 0;
    if (!manager || !decima::safeRead(manager + kManagerCount, count) || count <= 0 || count > kMaxInstances) return out;
    const uintptr_t instances = decima::readPointer(manager + kManagerArray);
    for (int32_t i = 0; instances && i < count; ++i) {
        const uintptr_t instance = decima::readPointer(instances + i * sizeof(uintptr_t));
        const uintptr_t resource = instance ? decima::readPointer(instance + kInstanceResource) : 0;
        LoadedNetwork network{};
        if (!resource || !decima::safeCopy(network.uuid, resource + kObjectUuid, kUuidSize)) continue;
        const uintptr_t main = decima::readPointer(instance + kInstanceRootSequence);
        if (main) {
            decima::safeRead(main - kSecondaryBase + kStopFrame, network.stopFrame);
            network.started = started(main - kSecondaryBase);
        }
        out.push_back(network);
    }
    return out;
}

void startNetwork(const uint8_t* uuid) {
    alignas(16) std::array<uint8_t, kUuidSize> key;
    std::copy(uuid, uuid + kUuidSize, key.begin());
    reinterpret_cast<void (*)(const void*)>(ds2::at(kStartNetworkByUuid))(key.data());
}

}  // namespace sequence_info
