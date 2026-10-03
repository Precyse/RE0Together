#include "ds2/remote_animation.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>

#include "anim_event.h"
#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/remote_player.h"
#include "hooks.h"
#include "log.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr uintptr_t kAnimationManagerRecord = 0x1441E72F0;
constexpr uintptr_t kMorphemeGetAnimatedPose = 0x140237280;  // MsgGetAnimatedPose handlers of the two concrete managers
constexpr uintptr_t kGraphGetAnimatedPose = 0x140237dc0;
constexpr uintptr_t kSetBool = 0x14023fc90;   // (manager, index, u8 value)
constexpr uintptr_t kSetFloat = 0x14023bc80;  // (manager, index, const float*): float and int variables
constexpr uintptr_t kSetQuat = 0x14023bd00;   // (manager, index, const float[4]*)

constexpr uintptr_t kComponentOwner = 0x48;
constexpr uintptr_t kVariableCount = 0x50;  // i32
constexpr uintptr_t kVariables = 0x58;      // array of 0x40-byte entries
constexpr size_t kVariableSize = 0x40;
constexpr uintptr_t kVariableName = 0x00;
constexpr uintptr_t kVariableGraphId = 0x08;  // -1: not bound to the graph
constexpr uintptr_t kVariableType = 0x0D;
constexpr uintptr_t kVariableValue = 0x20;
using remote_animation::kTypeBool;
using remote_animation::kTypeFloat;
using remote_animation::kTypeInt;
using remote_animation::kTypeQuat;
constexpr size_t kNameBytes = 48;
constexpr size_t kMaxVariables = 1024;
constexpr auto kSampleInterval = std::chrono::milliseconds(33);
constexpr auto kPeerStaleAfter = std::chrono::seconds(3);

using HandlerFn = void (*)(uintptr_t component, uintptr_t message);
using SetBoolFn = void (*)(uintptr_t manager, int index, uint8_t value);
using SetValueFn = void (*)(uintptr_t manager, int index, const float* value);

std::atomic<bool> g_mirrorLocal{false};
std::atomic<bool> g_collecting{false};
std::atomic<bool> g_snapshotRequested{true};
HandlerFn g_morpheme = nullptr;
HandlerFn g_graph = nullptr;

// Sending side: the value last reported for each variable.
struct Sent {
    bool valid = false;
    remote_animation::Change change{};
};
std::array<Sent, kMaxVariables> g_sent;
Clock::time_point g_lastSample;
std::mutex g_outboxMutex;
std::vector<remote_animation::Change> g_outbox;

// Receiving side: the partner's newest value of each variable.
struct Peer {
    bool valid = false;
    remote_animation::Change change{};
};
std::mutex g_peerMutex;
std::array<Peer, kMaxVariables> g_peer;
Clock::time_point g_peerAt;

uintptr_t managerOf(uintptr_t entity) { return ds2::componentByRecord(entity, kAnimationManagerRecord); }

std::string className(uintptr_t object) {
    char name[96] = {};
    const uintptr_t vtable = decima::readPointer(object);
    const uintptr_t locator = decima::readPointer(vtable - 8);
    uint32_t typeRva = 0;
    decima::safeRead(locator + 0xC, typeRva);
    decima::safeCopy(name, ds2::at(ds2::kImageBase) + typeRva + 0x10, sizeof(name) - 1);
    return name;
}

std::string variableName(uintptr_t variable) {
    char name[kNameBytes] = {};
    decima::safeCopy(name, decima::readPointer(variable + kVariableName), sizeof(name) - 1);
    return name;
}

// Once: do Sam and the remote have the same managers and the same variable table?
void logLayout(uintptr_t samManager, uintptr_t remoteManager) {
    const int32_t samCount = ds2::field<int32_t>(samManager, kVariableCount);
    const int32_t remoteCount = ds2::field<int32_t>(remoteManager, kVariableCount);
    int mismatches = 0;
    int bound = 0;
    for (int32_t i = 0; i < samCount && i < remoteCount; ++i) {
        const uintptr_t s = decima::readPointer(samManager + kVariables) + i * kVariableSize;
        const uintptr_t r = decima::readPointer(remoteManager + kVariables) + i * kVariableSize;
        if (ds2::field<int32_t>(s, kVariableGraphId) != -1) ++bound;
        if (variableName(s) != variableName(r)) ++mismatches;
    }
    logger::write("remote_animation: Sam's manager %s %d variables (%d bound), the remote's %s %d, %d names differ",
                  className(samManager).c_str(), samCount, bound, className(remoteManager).c_str(), remoteCount,
                  mismatches);
}

void writeVariable(uintptr_t manager, int index, uint8_t type, const uint8_t* value) {
    const auto setValue = reinterpret_cast<SetValueFn>(ds2::at(kSetFloat));
    switch (type) {
        case kTypeBool:
            reinterpret_cast<SetBoolFn>(ds2::at(kSetBool))(manager, index, value[0]);
            break;
        case kTypeInt: {
            int32_t integer;
            std::memcpy(&integer, value, sizeof(integer));
            const float asFloat = static_cast<float>(integer);
            setValue(manager, index, &asFloat);
            break;
        }
        case kTypeFloat: {
            float number;
            std::memcpy(&number, value, sizeof(number));
            setValue(manager, index, &number);
            break;
        }
        case kTypeQuat: {
            float quat[4];
            std::memcpy(quat, value, sizeof(quat));
            reinterpret_cast<SetValueFn>(ds2::at(kSetQuat))(manager, index, quat);
            break;
        }
        default:
            break;
    }
}

remote_animation::Change readVariable(uintptr_t variable, int index) {
    remote_animation::Change change{};
    change.index = static_cast<uint16_t>(index);
    change.type = ds2::field<uint8_t>(variable, kVariableType);
    std::memcpy(change.value, reinterpret_cast<const void*>(variable + kVariableValue),
                remote_animation::valueBytes(change.type));
    return change;
}

void mirrorLocalPlayer(uintptr_t remoteManager) {
    const uintptr_t samManager = managerOf(remote_player::samEntity());
    if (!samManager) return;
    static bool logged = false;
    if (!logged) {
        logged = true;
        logLayout(samManager, remoteManager);
    }
    const int32_t count = std::min(ds2::field<int32_t>(samManager, kVariableCount),
                                   ds2::field<int32_t>(remoteManager, kVariableCount));
    const uintptr_t source = decima::readPointer(samManager + kVariables);
    for (int32_t i = 0; i < count; ++i) {
        const uintptr_t variable = source + i * kVariableSize;
        if (ds2::field<int32_t>(variable, kVariableGraphId) == -1) continue;
        const remote_animation::Change change = readVariable(variable, i);
        writeVariable(remoteManager, i, change.type, change.value);
    }
}

void applyPartner(uintptr_t remoteManager) {
    std::lock_guard lock(g_peerMutex);
    if (Clock::now() - g_peerAt > kPeerStaleAfter) return;
    const int32_t count = std::min<int32_t>(ds2::field<int32_t>(remoteManager, kVariableCount), kMaxVariables);
    for (int32_t i = 0; i < count; ++i) {
        const Peer& peer = g_peer[i];
        if (peer.valid) writeVariable(remoteManager, i, peer.change.type, peer.change.value);
    }
    for (const remote_animation::Change& pulse : anim_event::active(remote_player::slot())) {
        if (pulse.index < count) writeVariable(remoteManager, pulse.index, pulse.type, pulse.value);
    }
}

// Samples the local player's variables: what changed since the last report (everything on a snapshot).
void sampleLocalPlayer(uintptr_t samManager) {
    const auto now = Clock::now();
    if (now - g_lastSample < kSampleInterval) return;
    g_lastSample = now;
    const bool snapshot = g_snapshotRequested.exchange(false);
    const int32_t count = std::min<int32_t>(ds2::field<int32_t>(samManager, kVariableCount), kMaxVariables);
    const uintptr_t source = decima::readPointer(samManager + kVariables);
    std::vector<remote_animation::Change> changes;
    for (int32_t i = 0; i < count; ++i) {
        const uintptr_t variable = source + i * kVariableSize;
        if (ds2::field<int32_t>(variable, kVariableGraphId) == -1) continue;
        const remote_animation::Change change = readVariable(variable, i);
        const size_t size = remote_animation::valueBytes(change.type);
        Sent& sent = g_sent[i];
        if (size == 0 || (!snapshot && sent.valid && std::memcmp(sent.change.value, change.value, size) == 0)) continue;
        sent = {true, change};
        changes.push_back(change);
    }
    if (changes.empty()) return;
    std::lock_guard lock(g_outboxMutex);
    g_outbox.insert(g_outbox.end(), changes.begin(), changes.end());
}

void beforePose(uintptr_t component) {
    const uintptr_t owner = ds2::field<uintptr_t>(component, kComponentOwner);
    const uintptr_t remote = remote_player::entity();
    if (remote && owner == remote) {
        if (g_mirrorLocal.load()) {
            mirrorLocalPlayer(component);
        } else {
            applyPartner(component);
        }
    } else if (g_collecting.load() && owner && owner == remote_player::samEntity()) {
        sampleLocalPlayer(component);
    }
}

void morphemeDetour(uintptr_t component, uintptr_t message) {
    beforePose(component);
    g_morpheme(component, message);
}

void graphDetour(uintptr_t component, uintptr_t message) {
    beforePose(component);
    g_graph(component, message);
}

}  // namespace

namespace remote_animation {

void installEarly() {
    hooks::install("morpheme animated pose", ds2::at(kMorphemeGetAnimatedPose),
                   reinterpret_cast<void*>(&morphemeDetour), reinterpret_cast<void**>(&g_morpheme));
    hooks::install("graph animated pose", ds2::at(kGraphGetAnimatedPose), reinterpret_cast<void*>(&graphDetour),
                   reinterpret_cast<void**>(&g_graph));
}

void setMirrorLocalPlayer(bool enabled) { g_mirrorLocal = enabled; }

bool mirrorsLocalPlayer() { return g_mirrorLocal.load(); }

void setCollecting(bool enabled) {
    if (enabled && !g_collecting.exchange(true)) g_snapshotRequested = true;
    if (!enabled) g_collecting = false;
}

void requestSnapshot() { g_snapshotRequested = true; }

std::vector<Change> takeLocalChanges() {
    std::lock_guard lock(g_outboxMutex);
    std::vector<Change> out;
    out.swap(g_outbox);
    return out;
}

void setPeerChange(uint8_t, const Change& change) {
    if (change.index >= kMaxVariables) return;
    std::lock_guard lock(g_peerMutex);
    g_peer[change.index] = {true, change};
    g_peerAt = Clock::now();
}

}  // namespace remote_animation
