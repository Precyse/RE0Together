#pragma once
// MSVC run-time type information of the game's own classes, read in-process: the primary vtable of a class by name,
// so objects can be recognised by their first qword.
#include <cstdint>

namespace msvc_rtti {

// Primary vtable (complete object, offset 0) of `className` (".?AV<name>@@" without the decoration), or 0. The first
// lookup of a name scans the image (about 60 ms); a vtable found is kept, so later lookups of it are free. Modules look up
// their names at start-up so no game thread ever pays for a scan. Any thread.
uintptr_t vtableOf(const char* className);

}  // namespace msvc_rtti
