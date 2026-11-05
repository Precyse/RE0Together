#pragma once
// Refuses to run against a DD2.exe other than the build names.h was pinned to (PE TimeDateStamp 0x6ae0ce34, Steam
// build 26256634), and next to DD2gether (both mods hook the same methods and redirect saves): the plugin then stays
// inert and solo play is untouched.
#include <cstdint>

namespace build_gate {

constexpr uint32_t kSupportedTimeDateStamp = 0x6ae0ce34;

// DD2gether's own marker file, relative to the game folder.
constexpr const wchar_t* kDd2getherMarker = L"\\reframework\\autorun\\coop\\version.lua";

// The PE TimeDateStamp in an image's headers (the loaded exe, or a copy of its first page); 0 when not a PE.
uint32_t timeDateStampOf(const void* image, uint32_t availableBytes);

// The running exe and the game folder pass; logs the reason when not.
bool checkRunningGame();

}  // namespace build_gate
