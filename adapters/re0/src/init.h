#pragma once
#include <windows.h>

// Thread entry: waits for the game code to be decrypted, then starts the subsystems. Never throws.
DWORD WINAPI initThread(LPVOID);

// Restores anything patched into the game.
void shutdownAdapter();
