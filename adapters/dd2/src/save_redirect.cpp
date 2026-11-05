#include "save_redirect.h"

#include <windows.h>

#include <string>

#include "calls.h"
#include "log.h"
#include "paths.h"
#include "refw.h"
#include "text.h"

namespace {

constexpr const char* kSaveService = "via.storage.saveService.SaveService";
constexpr const wchar_t* kSessionFolder = L"\\session";
constexpr const wchar_t* kSaveFolder = L"\\win64_save";  // where the launcher puts the host's world (games/dd2.json)

bool g_pending = false;
bool g_verified = false;

std::wstring pathOf(calls::Id getter, void* service) {
    const refw::InvokeResult* result = refw::invoke(calls::method(getter), service);
    return result && result->ptr ? refw::stringValue(result->ptr) : std::wstring();
}

bool samePath(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

void logPaths(const char* when, const std::wstring& dir, const std::wstring& mount) {
    logger::write("save_paths: %s dir=%s mount=%s", when, text::utf8(dir).c_str(), text::utf8(mount).c_str());
}

// True when done (set, or refused for good).
bool apply() {
    if (!calls::enabled(calls::Feature::Saves)) {
        logger::write("save_paths: REFUSED, SaveService calls missing; the real save is not protected");
        return true;
    }
    void* service = refw::nativeSingleton(kSaveService);
    if (!service) return false;
    const std::wstring session = coopDirectory() + kSessionFolder;
    CreateDirectoryW(session.c_str(), nullptr);
    logPaths("before", pathOf(calls::kGetSaveDirectoryPath, service), pathOf(calls::kGetSaveMountPath, service));
    void* path = refw::managedString(session);
    if (!path || !refw::invoke(calls::method(calls::kSetSaveMountPath), service, {path})) return false;
    const std::wstring dir = pathOf(calls::kGetSaveDirectoryPath, service), mount = pathOf(calls::kGetSaveMountPath, service);
    logPaths("after", dir, mount);
    g_verified = samePath(mount, session) && samePath(dir, session + kSaveFolder);
    if (!g_verified) logger::write("save_paths: REFUSED, the paths read back are not the session folder");
    return true;
}

}  // namespace

namespace save_redirect {

void enable() {
    g_pending = true;
    tick();
}

void tick() {
    if (g_pending && apply()) g_pending = false;
}

bool verified() { return g_verified; }

}  // namespace save_redirect
