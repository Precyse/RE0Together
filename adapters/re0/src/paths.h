#pragma once
#include <string>

// Directory of the host executable (the game folder).
std::wstring gameDirectory();

// <game dir>\coop, created on first call.
std::wstring coopDirectory();
