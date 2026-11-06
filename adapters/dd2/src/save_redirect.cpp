#include "save_redirect.h"

#include <windows.h>

#include <string>

#include "calls.h"
#include "log.h"
#include "refw.h"
#include "text.h"

namespace {

constexpr const char* kSaveService = "via.storage.saveService.SaveService";

constexpr uint64_t kGuardPeriodMs = 2000;  // how often a set redirect is checked against the game resetting it
constexpr bool kFileSystem = true;  // set_ForceWindows: the Windows file system instead of the Steam cloud

std::wstring g_mount;
bool g_pending = false;
bool g_verified = false;
uint64_t g_lastGuardMs = 0;
uint32_t g_resets = 0;

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
    logPaths("before", pathOf(calls::kGetSaveDirectoryPath, service), pathOf(calls::kGetSaveMountPath, service));
    void* path = refw::managedString(g_mount);
    if (!path || !refw::invoke(calls::method(calls::kSetForceWindows), service, {reinterpret_cast<void*>(uintptr_t{kFileSystem})}) ||
        !refw::invoke(calls::method(calls::kSetSaveMountPath), service, {path})) {
        return false;
    }
    const std::wstring dir = pathOf(calls::kGetSaveDirectoryPath, service), mount = pathOf(calls::kGetSaveMountPath, service);
    const refw::InvokeResult* windows = refw::invoke(calls::method(calls::kGetForceWindows), service);
    const bool fileSystem = windows && windows->byte != 0;
    logPaths("after", dir, mount);
    g_verified = fileSystem && samePath(mount, g_mount);  // the directory reads back as a slot pattern, not a path
    if (!g_verified) {
        logger::write("save_paths: REFUSED, %s", fileSystem ? "the mount read back is another folder" : "ForceWindows did not stay on");
    }
    return true;
}

// True when the game put the SaveService back on another mount or off the file system since we set it.
bool resetByGame() {
    void* service = refw::nativeSingleton(kSaveService);
    if (!service) return false;
    const refw::InvokeResult* windows = refw::invoke(calls::method(calls::kGetForceWindows), service);
    const bool fileSystem = windows && windows->byte != 0;
    const std::wstring mount = pathOf(calls::kGetSaveMountPath, service);
    if (fileSystem && samePath(mount, g_mount)) return false;
    logger::write("save_paths: the game set mount=%s ForceWindows=%d (reset %u), setting the redirect again", text::utf8(mount).c_str(),
                  fileSystem, ++g_resets);
    return true;
}

}  // namespace

namespace save_redirect {

void enable(const std::wstring& mount) {
    g_mount = mount;
    g_pending = !apply();
}

void tick(uint64_t nowMs) {
    if (g_mount.empty()) return;
    if (!g_pending && g_verified && nowMs - g_lastGuardMs >= kGuardPeriodMs) {
        g_lastGuardMs = nowMs;
        g_pending = resetByGame();
    }
    if (g_pending && apply()) g_pending = false;
}

bool verified() { return g_verified; }

}  // namespace save_redirect
