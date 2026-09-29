#include "save_redirect.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <string>

#include "debug_stats.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "paths.h"
#include "remote_storage_proxy.h"

namespace {

using remote_storage_proxy::Overrides;
using remote_storage_proxy::Proxy;

// steam_api import slot of SteamRemoteStorage in re0hd.exe (.rdata)
constexpr uintptr_t kImportSlot = 0xcb1458;
constexpr const wchar_t* kSteamApiModule = L"steam_api.dll";

// ISteamRemoteStorage v012 vtable slots
constexpr size_t kFileWriteSlot = 0;
constexpr size_t kFileReadSlot = 1;
constexpr size_t kFileDeleteSlot = 3;
constexpr size_t kFileExistsSlot = 10;
constexpr size_t kGetFileSizeSlot = 12;

constexpr ULONGLONG kLogIntervalMs = 1000;
constexpr const wchar_t* kTempSuffix = L".tmp";

enum class Op { Write, Read, Delete, Exists, Size, Count };
constexpr const char* kOpNames[] = {"write", "read", "delete", "exists", "size"};

// The real methods, called with the fastcall stand-in for thiscall (see game::callThiscall).
using WriteFn = bool(__fastcall*)(void*, void*, const char*, const void*, int32_t);
using ReadFn = int32_t(__fastcall*)(void*, void*, const char*, void*, int32_t);
using NameBoolFn = bool(__fastcall*)(void*, void*, const char*);
using NameSizeFn = int32_t(__fastcall*)(void*, void*, const char*);

using GetStorageFn = void*(__cdecl*)();
GetStorageFn g_original = nullptr;
save_redirect::WriteListener g_onCloudWrite = nullptr;
bool g_servingSession = false;
std::atomic<bool> g_activated{false};
std::atomic<ULONGLONG> g_lastLog[static_cast<size_t>(Op::Count)];

std::wstring sessionDirectory() { return gameDirectory() + L"\\coop\\session"; }

bool isPlainName(const char* name) {
    if (!name || !*name) return false;
    const std::string s(name);
    return s.find_first_of("\\/:") == std::string::npos && s != "." && s != "..";
}

// The session file for `name`, or empty when the name is not a plain file name or the session has no such file.
std::wstring sessionFile(const char* name) {
    if (!isPlainName(name)) return L"";
    const std::wstring path = sessionDirectory() + L"\\" + std::wstring(name, name + std::strlen(name));
    return GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES ? L"" : path;
}

void logCall(const char* name, Op op, long long size) {
    const auto index = static_cast<size_t>(op);
    const ULONGLONG now = GetTickCount64();
    if (g_lastLog[index] != 0 && now - g_lastLog[index] < kLogIntervalMs) return;
    g_lastLog[index] = now;
    logger::write("save_redirect: %s %s size=%lld", name, kOpNames[index], size);
}

template <class Fn>
Fn realSlot(const Proxy* self, size_t slot) {
    return reinterpret_cast<Fn>((*static_cast<void***>(self->real))[slot]);
}

struct FileHandle {
    HANDLE handle;
    explicit FileHandle(HANDLE h) : handle(h) {}
    ~FileHandle() {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    bool ok() const { return handle != INVALID_HANDLE_VALUE; }
};

bool writeFileAtomically(const std::wstring& path, const void* data, int32_t size) {
    const std::wstring temp = path + kTempSuffix;
    {
        FileHandle file(CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        DWORD written = 0;
        if (!file.ok() || !WriteFile(file.handle, data, static_cast<DWORD>(size), &written, nullptr) ||
            written != static_cast<DWORD>(size) || !FlushFileBuffers(file.handle)) {
            return false;
        }
    }
    return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool __fastcall proxyFileWrite(Proxy* self, void*, const char* name, const void* data, int32_t size) {
    const std::wstring path = sessionFile(name);
    if (path.empty()) {
        const bool written = realSlot<WriteFn>(self, kFileWriteSlot)(self->real, nullptr, name, data, size);
        if (written && g_onCloudWrite) g_onCloudWrite(name);
        return written;
    }
    debug_stats::count(debug_stats::Counter::SaveWrites);
    logCall(name, Op::Write, size);
    return writeFileAtomically(path, data, size);
}

int32_t __fastcall proxyFileRead(Proxy* self, void*, const char* name, void* data, int32_t size) {
    const std::wstring path = sessionFile(name);
    if (path.empty()) return realSlot<ReadFn>(self, kFileReadSlot)(self->real, nullptr, name, data, size);
    FileHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr));
    DWORD read = 0;
    if (!file.ok() || !ReadFile(file.handle, data, static_cast<DWORD>(size), &read, nullptr)) read = 0;
    debug_stats::count(debug_stats::Counter::SaveReads);
    logCall(name, Op::Read, read);
    return static_cast<int32_t>(read);
}

bool __fastcall proxyFileDelete(Proxy* self, void*, const char* name) {
    const std::wstring path = sessionFile(name);
    if (path.empty()) return realSlot<NameBoolFn>(self, kFileDeleteSlot)(self->real, nullptr, name);
    logCall(name, Op::Delete, 0);
    return DeleteFileW(path.c_str()) != 0;
}

bool __fastcall proxyFileExists(Proxy* self, void*, const char* name) {
    if (sessionFile(name).empty()) return realSlot<NameBoolFn>(self, kFileExistsSlot)(self->real, nullptr, name);
    logCall(name, Op::Exists, 0);
    return true;
}

int32_t __fastcall proxyGetFileSize(Proxy* self, void*, const char* name) {
    const std::wstring path = sessionFile(name);
    if (path.empty()) return realSlot<NameSizeFn>(self, kGetFileSizeSlot)(self->real, nullptr, name);
    WIN32_FILE_ATTRIBUTE_DATA info{};
    const bool ok = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) != 0;
    const int32_t size = ok ? static_cast<int32_t>(info.nFileSizeLow) : 0;
    logCall(name, Op::Size, size);
    return size;
}

const Overrides& overrides() {
    static const Overrides table = [] {
        Overrides t{};
        t[kFileWriteSlot] = reinterpret_cast<void*>(proxyFileWrite);
        t[kFileReadSlot] = reinterpret_cast<void*>(proxyFileRead);
        t[kFileDeleteSlot] = reinterpret_cast<void*>(proxyFileDelete);
        t[kFileExistsSlot] = reinterpret_cast<void*>(proxyFileExists);
        t[kGetFileSizeSlot] = reinterpret_cast<void*>(proxyGetFileSize);
        return t;
    }();
    return table;
}

void* __cdecl hookedSteamRemoteStorage() {
    void* real = g_original();
    if (!real) return nullptr;
    if (!g_activated.exchange(true)) {
        debug_stats::set(debug_stats::Gauge::SaveRedirect, g_servingSession ? 1 : 0);
        if (g_servingSession) logger::write("save_redirect: active, serving %ls", sessionDirectory().c_str());
        else logger::write("save_redirect: active, Steam cloud with write reports");
    }
    return remote_storage_proxy::get(real, overrides());
}

bool sessionHasFiles() {
    WIN32_FIND_DATAW found;
    HANDLE find = FindFirstFileW((sessionDirectory() + L"\\*").c_str(), &found);
    if (find == INVALID_HANDLE_VALUE) return false;
    bool any = false;
    do {
        any = !(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    } while (!any && FindNextFileW(find, &found));
    FindClose(find);
    return any;
}

bool writeImport(uint32_t value) {
    DWORD oldProtect = 0;
    void* slot = reinterpret_cast<void*>(kImportSlot);
    if (!VirtualProtect(slot, sizeof(value), PAGE_READWRITE, &oldProtect)) return false;
    *static_cast<volatile uint32_t*>(slot) = value;
    VirtualProtect(slot, sizeof(value), oldProtect, &oldProtect);
    return true;
}

bool importPointsIntoSteamApi(uintptr_t target) {
    HMODULE owner = nullptr;
    return target &&
           GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(target), &owner) &&
           owner == GetModuleHandleW(kSteamApiModule);
}

// The save stamps its owner's SteamID (0x6126e0) and the load path rejects a different account (0x612600).
// The session copy belongs to the host, so while it is being served the owner check passes.
using IsSaveOwnerFn = bool(__fastcall*)(void* self, void* edx);
IsSaveOwnerFn g_originalIsSaveOwner = nullptr;

bool __fastcall isSaveOwnerDetour(void*, void*) { return true; }

}  // namespace

namespace save_redirect {

bool install(WriteListener onCloudWrite) {
    g_onCloudWrite = onCloudWrite;
    g_servingSession = sessionHasFiles();
    const uintptr_t original = game::readPointer(kImportSlot);
    if (!importPointsIntoSteamApi(original)) {
        logger::write("save_redirect: import 0x%x does not point into steam_api, inactive",
                      static_cast<unsigned>(kImportSlot));
        return false;
    }
    g_original = reinterpret_cast<GetStorageFn>(original);
    if (!writeImport(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hookedSteamRemoteStorage)))) {
        logger::write("save_redirect: cannot patch the import");
        return false;
    }
    logger::write("save_redirect: import patched");
    if (!g_servingSession) return true;
    return hooks::install("save owner check", game::kSaveOwnerCheckFunction, reinterpret_cast<void*>(&isSaveOwnerDetour),
                          reinterpret_cast<void**>(&g_originalIsSaveOwner));
}

void uninstall() {
    if (g_original) writeImport(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_original)));
    if (g_originalIsSaveOwner) hooks::remove(game::kSaveOwnerCheckFunction);
}

}  // namespace save_redirect
