#pragma once
#include <windows.h>

// Adapter start-up thread spawned from DllMain: config, crash dumps, overlay hooks, then the launcher link once
// the game's engine objects exist.
DWORD WINAPI initThread(LPVOID);
