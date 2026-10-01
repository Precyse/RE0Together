#include "documents_redirect.h"

#include <windows.h>

#include <knownfolders.h>
#include <objbase.h>
#include <shlobj.h>

#include <cstring>
#include <string>

#include "import_patch.h"
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

}  // namespace

namespace documents_redirect {

void install() {
    g_documents = coopDirectory() + kSessionDocuments;
    if (GetFileAttributesW(g_documents.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    g_knownFolder = reinterpret_cast<KnownFolderFn>(
        import_patch::redirect(kShellDll, "SHGetKnownFolderPath", reinterpret_cast<void*>(&knownFolderDetour)));
    g_folderPath = reinterpret_cast<FolderPathFn>(
        import_patch::redirect(kShellDll, "SHGetFolderPathW", reinterpret_cast<void*>(&folderPathDetour)));
}

bool active() { return g_knownFolder && g_folderPath; }

}  // namespace documents_redirect
