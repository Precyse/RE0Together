#pragma once
#include <windows.h>

#include <string>

// The path of a system DLL as this 32-bit process loads it (SysWOW64 on a 64-bit Windows, else System32), so a copy in the game
// folder is never picked up.
inline std::wstring systemDllPath(const wchar_t* name) {
    wchar_t dir[MAX_PATH];
    UINT n = GetSystemWow64DirectoryW(dir, MAX_PATH);
    if (n == 0) n = GetSystemDirectoryW(dir, MAX_PATH);
    return std::wstring(dir, n) + L"\\" + name;
}
