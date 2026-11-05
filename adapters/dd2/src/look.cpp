#include "look.h"

#include "calls.h"
#include "log.h"
#include "refw.h"
#include "serializer.h"

namespace {

constexpr uint32_t kHolderContexts = 0x58;      // ContextHolder.Contexts: Dictionary<Type, ContextInfoBase>
constexpr uint32_t kHolderHumanStatus = 0x70;   // ContextHolder.HumanStatusContext
constexpr uint32_t kInfoCurrentContext = 0x10;  // ContextInfoBase.<CurrentContext>

// The current context of `type` in a ContextHolder's dictionary, or null.
void* contextOf(void* contexts, const char* type) {
    void* info = nullptr;
    const refw::InvokeResult* found = refw::invoke(calls::method(calls::kContextsTryGetValue), contexts, {refw::typeOf(type), &info});
    if (!found || !found->byte || !info) return nullptr;
    return refw::field<void*>(info, kInfoCurrentContext);
}

}  // namespace

namespace look {

std::optional<Objects> of(void* character) {
    void* holder = character ? calls::objectResult(calls::kGetContext, character) : nullptr;
    if (!holder) return std::nullopt;
    Objects out{};
    out[0] = refw::field<void*>(holder, kHolderHumanStatus);
    void* contexts = refw::field<void*>(holder, kHolderContexts);
    for (size_t i = 1; i < kCount; ++i) out[i] = contexts ? contextOf(contexts, kSlots[i].type) : nullptr;
    return out;
}

bool toJson(const Objects& objects, Json& out) {
    out = {};
    for (size_t i = 0; i < kCount; ++i) {
        if (!objects[i]) continue;
        const auto json = serializer::toJson(kSlots[i].type, objects[i]);
        if (!json) return false;
        out[i] = *json;
    }
    return true;
}

bool fromJson(const Json& json, Objects& out) {
    out = {};
    for (size_t i = 0; i < kCount; ++i) {
        if (json[i].empty()) continue;
        out[i] = serializer::fromJson(kSlots[i].type, json[i]);
        if (!out[i]) {
            logger::write("look: %s refused by the deserializer", kSlots[i].type);
            release(out);
            return false;
        }
    }
    return true;
}

void release(Objects& objects) {
    for (void*& object : objects) {
        if (object) refw::release(object);
        object = nullptr;
    }
}

void* writer(const Objects& objects) {
    void* out = refw::createInstance("app.ContextWriterOnline");
    if (!out) return nullptr;
    for (size_t i = 0; i < kCount; ++i) {
        if (objects[i]) refw::writeRef(out, kSlots[i].writerOffset, objects[i]);
    }
    return out;
}

}  // namespace look
