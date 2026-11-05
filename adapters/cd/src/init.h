#pragma once
#include <windows.h>

// Adapter start-up thread spawned from DllMain: crash dumps, config, the build gate, then (once the title screen's
// WorldSystem exists, so Denuvo has run) the Troc catalogue, the hook sets and the launcher link.
DWORD WINAPI initThread(LPVOID);
