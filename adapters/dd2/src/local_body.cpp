#include "local_body.h"

#include <cmath>
#include <cstring>

#include "calls.h"
#include "refw.h"
#include "wire_dd2.h"

namespace {

constexpr const char* kCharacterManager = "app.CharacterManager";
constexpr const char* kPawnManager = "app.PawnManager";

}  // namespace

namespace local_body {

void* character(uint8_t kind) {
    if (!calls::enabled(calls::Feature::Body)) return nullptr;
    if (kind == wire_dd2::kKindArisen) {
        void* manager = refw::managedSingleton(kCharacterManager);
        return manager ? calls::objectResult(calls::kGetManualPlayer, manager) : nullptr;
    }
    void* pawns = refw::managedSingleton(kPawnManager);
    void* pawn = pawns ? calls::objectResult(calls::kGetMainPawn, pawns) : nullptr;
    return pawn ? calls::objectResult(calls::kGetCachedCharacter, pawn) : nullptr;
}

std::optional<Sample> sample(uint8_t kind) { return sampleCharacter(character(kind)); }

std::optional<Sample> sampleCharacter(void* character) {
    if (!calls::enabled(calls::Feature::Body)) return std::nullopt;
    void* transform = character ? calls::objectResult(calls::kGetTransform, character) : nullptr;
    if (!transform) return std::nullopt;

    Sample sample;
    const refw::InvokeResult* position = refw::invoke(calls::method(calls::kGetUniversalPosition), transform);
    if (!position) return std::nullopt;
    std::memcpy(sample.position, position->bytes, sizeof(sample.position));
    const refw::InvokeResult* rotation = refw::invoke(calls::method(calls::kGetRotation), transform);
    if (!rotation) return std::nullopt;
    float quaternion[4];
    std::memcpy(quaternion, rotation->bytes, sizeof(quaternion));
    sample.yaw = yawOf(quaternion);

    if (void* actions = calls::objectResult(calls::kGetActionManager, character)) {
        for (uint32_t layer = 0; layer < kSampledLayers; ++layer) {
            const refw::InvokeResult* hash = refw::invoke(calls::method(calls::kGetCurrentActionHash), actions,
                                                          {reinterpret_cast<void*>(static_cast<uintptr_t>(layer))});
            sample.actionHash[layer] = hash ? hash->dword : 0;
        }
    }
    if (const refw::InvokeResult* hp = refw::invoke(calls::method(calls::kGetHp), character)) sample.hp = hp->f;
    if (const refw::InvokeResult* dead = refw::invoke(calls::method(calls::kGetIsDead), character)) sample.dead = dead->byte != 0;
    if (const refw::InvokeResult* speed = refw::invoke(calls::method(calls::kGetDiscreteSpeedType), character)) {
        sample.speedType = static_cast<int32_t>(speed->dword);
    }
    for (double axis : sample.position) {
        if (!std::isfinite(axis)) return std::nullopt;
    }
    return sample;
}

}  // namespace local_body
