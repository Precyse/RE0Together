#include "content_hash.h"

#include <windows.h>

#include <string>
#include <vector>

#include "content_hash_rules.h"
#include "log.h"
#include "script_override.h"

namespace {

using content_hash_rules::File;

constexpr LONGLONG kMaxFileBytes = 0x7FFFFFFF;  // a size must stay below content_hash_rules::kUnreadableSize

// The path of the dll this code is in.
std::wstring ownPath() {
    HMODULE module = nullptr;
    const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (!GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(&ownPath), &module)) return {};
    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    return length == 0 || length >= MAX_PATH ? std::wstring() : std::wstring(path, length);
}

File readFile(const std::wstring& path) {
    if (path.empty()) return std::nullopt;
    const HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return std::nullopt;
    File file;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(handle, &size) && size.QuadPart <= kMaxFileBytes) {
        std::vector<uint8_t> bytes(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (bytes.empty() || (ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size())) {
            file = std::move(bytes);
        }
    }
    CloseHandle(handle);
    return file;
}

File readScript(const std::string& name) {
    return readFile(script_override::directory() + L"\\" + std::wstring(name.begin(), name.end()));
}

}  // namespace

namespace content_hash {

uint64_t compute() {
    const content_hash_rules::Result result =
        content_hash_rules::compute(readFile(ownPath()), script_override::names(), readScript);
    for (const std::string& name : result.unreadable) logger::write("version gate: %s cannot be read, it never matches", name.c_str());
    logger::write("version gate: content hash 0x%016llX over dinput8.dll (%zu bytes) and %zu scripts",
                  static_cast<unsigned long long>(result.hash), result.dllBytes, result.scripts);
    return result.hash;
}

}  // namespace content_hash
