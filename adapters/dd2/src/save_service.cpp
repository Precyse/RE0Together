#include "save_service.h"

#include <optional>

#include "calls.h"
#include "refw.h"

namespace {

constexpr const char* kStateEnum = "via.storage.saveService.SaveState";
constexpr const char* kIdleLiteral = "IDLE";

std::optional<int64_t> idleLiteral() {
    static const std::optional<int64_t> literal = refw::enumLiteral(kStateEnum, kIdleLiteral);
    return literal;
}

}  // namespace

namespace save_service {

void* instance() { return refw::nativeSingleton(kType); }

bool idle(void* service) {
    const auto literal = idleLiteral();
    if (!service || !literal || !calls::enabled(calls::Feature::Saves)) return false;
    const refw::InvokeResult* state = refw::invoke(calls::method(calls::kGetSaveState), service);
    return state && static_cast<int64_t>(state->dword) == *literal;
}

bool idleKnown() { return idleLiteral().has_value(); }

}  // namespace save_service
