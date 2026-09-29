#include "paths.h"

#include <windows.h>

std::wstring gameDirectory() {
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full(path, n);
    return full.substr(0, full.find_last_of(L"\\/"));
}

std::wstring coopDirectory() {
    const std::wstring dir = gameDirectory() + L"\\coop";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}
