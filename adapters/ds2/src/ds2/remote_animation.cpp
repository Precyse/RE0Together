#include "ds2/remote_animation.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/remote_player.h"
#include "hooks.h"
#include "log.h"

namespace {

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
constexpr uint8_t kTypeBool = 0;
constexpr uint8_t kTypeInt = 1;
constexpr uint8_t kTypeFloat = 2;
constexpr uint8_t kTypeQuat = 3;
constexpr size_t kNameBytes = 48;

using HandlerFn = void (*)(uintptr_t component, uintptr_t message);
using SetBoolFn = void (*)(uintptr_t manager, int index, uint8_t value);
using SetValueFn = void (*)(uintptr_t manager, int index, const float* value);

std::atomic<bool> g_mirrorLocal{false};
HandlerFn g_morpheme = nullptr;
HandlerFn g_graph = nullptr;

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

void copyVariable(uintptr_t target, int index, uintptr_t source) {
    const auto setValue = reinterpret_cast<SetValueFn>(ds2::at(kSetFloat));
    switch (ds2::field<uint8_t>(source, kVariableType)) {
        case kTypeBool:
            reinterpret_cast<SetBoolFn>(ds2::at(kSetBool))(target, index, ds2::field<uint8_t>(source, kVariableValue));
            break;
        case kTypeInt: {
            const float value = static_cast<float>(ds2::field<int32_t>(source, kVariableValue));
            setValue(target, index, &value);
            break;
        }
        case kTypeFloat:
            setValue(target, index, reinterpret_cast<const float*>(source + kVariableValue));
            break;
        case kTypeQuat:
            reinterpret_cast<SetValueFn>(ds2::at(kSetQuat))(target, index,
                                                            reinterpret_cast<const float*>(source + kVariableValue));
            break;
        default:
            break;
    }
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
        if (ds2::field<int32_t>(variable, kVariableGraphId) != -1) copyVariable(remoteManager, i, variable);
    }
}

void beforePose(uintptr_t component) {
    const uintptr_t remote = remote_player::entity();
    if (!remote || ds2::field<uintptr_t>(component, kComponentOwner) != remote) return;
    if (g_mirrorLocal.load()) mirrorLocalPlayer(component);
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

}  // namespace remote_animation
