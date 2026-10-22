#pragma once
#include <windows.h>

// Thread entry: logs, reads the ini, checks the exe build and installs the hooks. Never throws.
DWORD WINAPI initThread(LPVOID);
