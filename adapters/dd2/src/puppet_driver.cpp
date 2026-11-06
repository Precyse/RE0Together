#include "puppet_driver.h"

#include <cmath>
#include <optional>

#include "calls.h"
#include "local_body.h"
#include "log.h"
#include "names.h"
#include "refw.h"

namespace {

constexpr double kWarpM = 1.5;      // farther than this the body is warped
constexpr double kArriveM = 0.3;    // closer than this it turns along the owner's facing
constexpr double kFacingAheadM = 2.0;
constexpr bool kNoForce = false;    // the Boolean argument of the request methods
// Think stop for humans (no ThinkStopRequestController on them): the flag that controller holds every frame, set the same
// way (static_checks.md, phase 4): Character.AIDecisionMaker -> DecisionModule -> IsSkipThink.
constexpr uint32_t kCharacterDecisionMaker = 0x168;
constexpr uint32_t kMakerDecisionModule = 0x68;
constexpr uint32_t kModuleSkipThink = 0x74;

std::optional<int64_t> g_idle;
void* g_warpOption = nullptr;

void* asArg(uint64_t value) { return reinterpret_cast<void*>(static_cast<uintptr_t>(value)); }

double distance(const double a[3], const double b[3]) {
    const double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void holdThinkStop(void* character) {
    void* maker = refw::field<void*>(character, kCharacterDecisionMaker);
    void* module = maker ? refw::field<void*>(maker, kMakerDecisionModule) : nullptr;
    if (module) refw::field<bool>(module, kModuleSkipThink) = true;
}

void turnToward(const puppet_registry::Body& body, const double point[3]) {
    if (!body.turn) return;
    double at[3] = {point[0], point[1], point[2]};
    refw::invoke(calls::method(calls::kSetTurnDestination), body.turn, {at});
}

void locomote(const puppet_registry::Body& body, int32_t speed, puppet_driver::State& state) {
    if (!body.selector || speed == state.lastSpeed) return;
    state.lastSpeed = speed;
    if (g_idle && speed == *g_idle) {
        refw::invoke(calls::method(calls::kRequestIdle), body.selector, {asArg(kNoForce)});
    } else {
        refw::invoke(calls::method(calls::kRequestLocomotion), body.selector, {asArg(static_cast<uint32_t>(speed)), asArg(kNoForce)});
    }
}

}  // namespace

namespace puppet_driver {

bool init() {
    for (const auto& [name, value] : refw::enumLiterals(names::kSpeedTypeEnum)) {
        if (name == "Idle") g_idle = value;
    }
    refw::staticField("app.CharacterWarpOption", "NoChangePosRotContext", g_warpOption);
    logger::write("puppet_driver: idle literal %s, warp option %s", g_idle ? "found" : "missing", g_warpOption ? "found" : "missing");
    return g_idle.has_value() && g_warpOption;
}

bool warpTo(void* character, const double position[3], float yaw) {
    double at[3] = {position[0], position[1], position[2]};
    float rotation[4] = {0.0f, std::sin(yaw / 2), 0.0f, std::cos(yaw / 2)};
    return g_warpOption && refw::invoke(calls::method(calls::kWarp), character, {at, rotation, g_warpOption}) != nullptr;
}

void drive(const puppet_registry::Body& body, const wire_dd2::BodyState& target, std::span<const wire_dd2::ActionEvent> edges,
           State& state) {
    const puppet_registry::DriverScope scope;
    holdThinkStop(body.character);
    const auto now = local_body::sampleCharacter(body.character);
    if (!now) return;
    const double off = distance(now->position, target.position);
    if (!state.warped || off > kWarpM) {
        warpTo(body.character, target.position, target.yaw);
        state.warped = true;
        state.lastSpeed = -1;
    } else if (off > kArriveM) {
        turnToward(body, target.position);
        locomote(body, static_cast<int32_t>(target.speedType), state);
    } else {
        const double ahead[3] = {target.position[0] + std::sin(target.yaw) * kFacingAheadM, target.position[1],
                                 target.position[2] + std::cos(target.yaw) * kFacingAheadM};
        turnToward(body, ahead);
        locomote(body, static_cast<int32_t>(target.speedType), state);
    }
    if (!body.actions) return;
    for (const wire_dd2::ActionEvent& edge : edges) {
        refw::invoke(calls::method(calls::kRequestActionByHash), body.actions,
                     {asArg(edge.actionHash), asArg(edge.layer), asArg(kNoForce)});
    }
}

}  // namespace puppet_driver
