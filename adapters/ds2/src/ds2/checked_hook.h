#pragma once
// DS2-internal: a detour that is only installed when the function still starts with the bytes the adapter was written for.
#include <cstdint>

namespace checked_hook {

// Installs the detour on the function at `fileVa` when its first bytes match `expected` (hex bytes, "??" any byte). On a
// mismatch nothing is hooked, the reason is logged and the caller's feature stays off. False then, and when the hook fails.
bool install(const char* name, uintptr_t fileVa, const char* expected, void* detour, void** original);

}  // namespace checked_hook
