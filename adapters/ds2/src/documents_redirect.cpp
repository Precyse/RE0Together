#include "documents_redirect.h"

#include <windows.h>

#include <knownfolders.h>
#include <objbase.h>
#include <shlobj.h>

#include <cstring>
#include <string>

#include "paths.h"

namespace {

constexpr const wchar_t* kSessionDocuments = L"\\session\\Documents";
constexpr const char* kShellDll = "SHELL32.dll";
constexpr int kCsidlMask = 0xFF;  // SHGetFolderPathW: the low byte is the folder, the rest are flags

using KnownFolderFn = HRESULT(WINAPI*)(REFKNOWNFOLDERID, DWORD, HANDLE, PWSTR*);
using FolderPathFn = HRESULT(WINAPI*)(HWND, int, HANDLE, DWORD, LPWSTR);

KnownFolderFn g_knownFolder = nullptr;
FolderPathFn g_folderPath = nullptr;
std::wstring g_documents;

HRESULT WINAPI knownFolderDetour(REFKNOWNFOLDERID id, DWORD flags, HANDLE token, PWSTR* path) {
    if (!IsEqualGUID(id, FOLDERID_Documents) || !path) return g_knownFolder(id, flags, token, path);
    const size_t bytes = (g_documents.size() + 1) * sizeof(wchar_t);
    *path = static_cast<PWSTR>(CoTaskMemAlloc(bytes));
    if (!*path) return E_OUTOFMEMORY;
    std::memcpy(*path, g_documents.c_str(), bytes);
    return S_OK;
}

HRESULT WINAPI folderPathDetour(HWND window, int csidl, HANDLE token, DWORD flags, LPWSTR path) {
    if ((csidl & kCsidlMask) != CSIDL_PERSONAL || !path) return g_folderPath(window, csidl, token, flags, path);
    wcsncpy_s(path, MAX_PATH, g_documents.c_str(), _TRUNCATE);
    return S_OK;
}

// Points the game's own import slot for `name` (from SHELL32.dll) at `detour`; returns the original target.
void* patchImport(const char* name, void* detour) {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc) {
        if (_stricmp(reinterpret_cast<const char*>(base + desc->Name), kShellDll) != 0) continue;
        auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(byName->Name, name) != 0) continue;
            DWORD old = 0;
            VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &old);
            void* original = reinterpret_cast<void*>(slots->u1.Function);
            slots->u1.Function = reinterpret_cast<ULONGLONG>(detour);
            VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), old, &old);
            return original;
        }
    }
    return nullptr;
}

}  // namespace

namespace documents_redirect {

void install() {
    g_documents = coopDirectory() + kSessionDocuments;
    if (GetFileAttributesW(g_documents.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    g_knownFolder = reinterpret_cast<KnownFolderFn>(
        patchImport("SHGetKnownFolderPath", reinterpret_cast<void*>(&knownFolderDetour)));
    g_folderPath =
        reinterpret_cast<FolderPathFn>(patchImport("SHGetFolderPathW", reinterpret_cast<void*>(&folderPathDetour)));
}

bool active() { return g_knownFolder && g_folderPath; }

}  // namespace documents_redirect
