#pragma once
// The engine's own reflection JSON (via.Serializer, static rows of calls.h): a managed object of a named type to UTF-8
// JSON and back. The look of a puppet and the co-op profile travel this way.
#include <optional>
#include <string>

namespace serializer {

// The object as JSON, or null when the serializer refused it.
std::optional<std::string> toJson(const std::string& type, void* object);

// A new object of `type` from JSON holding one reference of ours (refw::release it), or null when refused.
void* fromJson(const std::string& type, const std::string& json);

}  // namespace serializer
