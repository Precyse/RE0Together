#include "serializer.h"

#include <cstdint>

#include "calls.h"
#include "refw.h"
#include "text.h"

namespace {

constexpr uintptr_t kSerializerDefaults = 0;  // via.Serializer.Options

void* options() { return reinterpret_cast<void*>(kSerializerDefaults); }

}  // namespace

namespace serializer {

std::optional<std::string> toJson(const std::string& type, void* object) {
    void* typeObject = refw::typeOf(type.c_str());
    if (!object || !typeObject) return std::nullopt;
    const refw::InvokeResult* result = refw::invoke(calls::method(calls::kSerializeJson), nullptr, {typeObject, object, options()});
    if (!result || !result->ptr) return std::nullopt;
    return text::utf8(refw::stringValue(result->ptr));
}

void* fromJson(const std::string& type, const std::string& json) {
    void* typeObject = refw::typeOf(type.c_str());
    void* string = json.empty() ? nullptr : refw::managedString(text::utf16(json));
    if (!typeObject || !string) return nullptr;
    const refw::InvokeResult* result = refw::invoke(calls::method(calls::kDeserializeJson), nullptr, {typeObject, string, options()});
    if (!result || !result->ptr) return nullptr;
    refw::addRef(result->ptr);
    return result->ptr;
}

}  // namespace serializer
