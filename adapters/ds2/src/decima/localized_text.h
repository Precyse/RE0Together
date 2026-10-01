#pragma once
// Decima LocalizedTextResource: a display string in the game's current language.
#include <cstdint>
#include <string>

namespace decima {

// The UTF-8 text of the LocalizedTextResource at `resource` (cut at 63 bytes), or "" when unreadable.
std::string localizedText(uintptr_t resource);

}  // namespace decima
