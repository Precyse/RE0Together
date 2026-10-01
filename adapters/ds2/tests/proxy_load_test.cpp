// Loads the built version.dll proxy by path and calls a forwarded export: proves the forwarders reach the system DLL.
#include <windows.h>

#include <cstdio>
#include <string>

namespace {

using GetFileVersionInfoSizeWFn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);

std::wstring besideThisExe(const wchar_t* name) {
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path, n);
    return dir.substr(0, dir.find_last_of(L"\\/") + 1) + name;
}

}  // namespace

int main() {
    const HMODULE proxy = LoadLibraryW(besideThisExe(L"version.dll").c_str());
    if (!proxy) {
        std::printf("FAIL: cannot load the proxy (%lu)\n", GetLastError());
        return 1;
    }
    const auto sizeOf = reinterpret_cast<GetFileVersionInfoSizeWFn>(GetProcAddress(proxy, "GetFileVersionInfoSizeW"));
    wchar_t kernel32[MAX_PATH];
    GetSystemDirectoryW(kernel32, MAX_PATH);
    const std::wstring target = std::wstring(kernel32) + L"\\kernel32.dll";
    DWORD handle = 0;
    const DWORD size = sizeOf ? sizeOf(target.c_str(), &handle) : 0;
    std::printf("%s: GetFileVersionInfoSizeW(kernel32) = %lu\n", size ? "PASS" : "FAIL", size);
    return size ? 0 : 1;
}
