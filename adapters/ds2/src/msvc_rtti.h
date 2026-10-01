#pragma once
// MSVC run-time type information of the game's own classes, read in-process: the primary vtable of a class by name,
// so objects can be recognised by their first qword.
#include <cstdint>

namespace msvc_rtti {

// Primary vtable (complete object, offset 0) of `className` (".?AV<name>@@" without the decoration), or 0.
uintptr_t vtableOf(const char* className);

}  // namespace msvc_rtti
