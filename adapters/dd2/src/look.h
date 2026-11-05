#pragma once
// A character's look as the stray-pawn route takes it (coop-scratch\dd2\design\static_checks.md): the HumanStatus
// context and six contexts from the ContextHolder's dictionary, each written into its ContextWriterOnline field. The
// local Arisen's are read in place; a peer's travel as the engine's own reflection JSON (via.Serializer) and are
// rebuilt here as new objects.
#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace look {

struct Slot {
    const char* type;
    uint32_t writerOffset;  // its ContextWriterOnline field
};

// Index 0 is the holder's HumanStatusContext field; the others come from its Contexts dictionary.
constexpr std::array<Slot, 7> kSlots{{
    {"app.HumanStatusContext", 0x10}, {"app.WeaponContext", 0x18},    {"app.JobContext", 0x20},
    {"app.PartSwapHumanContext", 0x48}, {"app.BodyEditContext", 0x50}, {"app.FaceEditContext", 0x58},
    {"app.BodyDetailContext", 0x60},
}};
constexpr size_t kCount = kSlots.size();

using Objects = std::array<void*, kCount>;  // null where missing
using Json = std::array<std::string, kCount>;  // UTF-8, empty where missing

// A character's look contexts, borrowed (no reference of ours); null for null or a character without a ContextHolder.
std::optional<Objects> of(void* character);

// Each present context as JSON; false when the serializer refused one.
bool toJson(const Objects& objects, Json& out);

// New contexts from JSON, each holding one reference of ours (release them with release()); false when the
// deserializer refused one (nothing is kept then).
bool fromJson(const Json& json, Objects& out);
void release(Objects& objects);

// A new ContextWriterOnline (with its constructor: its default pawn data, official work and the rest are read without
// null checks) carrying these contexts; one reference of ours, or null.
void* writer(const Objects& objects);

}  // namespace look
