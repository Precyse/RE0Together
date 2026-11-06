#include "offline_capture.h"

#include <windows.h>

#include <fstream>
#include <optional>
#include <string>

#include "calls.h"
#include "log.h"
#include "paths.h"
#include "profile.h"
#include "refw.h"
#include "save_sections.h"

namespace {

constexpr const char* kSaveService = "via.storage.saveService.SaveService";
constexpr const char* kStateEnum = "via.storage.saveService.SaveState";
constexpr const char* kResultEnum = "via.storage.saveService.SaveResult";
constexpr const char* kIdleLiteral = "IDLE";
constexpr const char* kSuccessLiteral = "Success";
constexpr const char* kDoingLiteral = "Doing";
constexpr const char* kGameSaveDataType = "app.SaveDataManager.GameSaveData";
constexpr const wchar_t* kGameDataClass = L"GAME_SAVE_DATA";  // the SaveService's name for a game save's root class
constexpr uint32_t kGameSaveDataList = 0x10;                   // GameSaveData.GameSaveDataList, List<app.SaveDataBase>
constexpr uint64_t kTimeoutMs = 60000;
constexpr uint64_t kTitleSettleMs = 3000;
constexpr uint64_t kSectionsWaitMs = 10000;  // how long the root's list may take to fill after the load succeeded  // the title menu shown this long with the SaveService idle: the game's own loads are done
constexpr const char* kMainFlowManager = "app.MainFlowManager";
constexpr const wchar_t* kResultFile = L"\\capture.result";

enum class Step { Off, Waiting, Loading, Done };

Step g_step = Step::Off;
int g_slot = -1;
uint64_t g_startMs = 0;
uint64_t g_titleSinceMs = 0;
uint64_t g_loadedMs = 0;  // when the load reported success; 0 before  // when the title menu was first seen idle; 0 while it is not
void* g_root = nullptr;  // the scratch GameSaveData, one reference of ours
std::optional<int64_t> g_idle, g_success, g_doing;

std::optional<int64_t> literal(const char* type, const char* name) {
    for (const auto& [each, value] : refw::enumLiterals(type)) {
        if (each == name) return value;
    }
    return std::nullopt;
}

int64_t dword(calls::Id getter, void* service) {
    const refw::InvokeResult* result = refw::invoke(calls::method(getter), service);
    return result ? static_cast<int64_t>(result->dword) : -1;
}

void finish(void* service, const std::string& outcome) {
    std::ofstream(coopDirectory() + kResultFile, std::ios::trunc) << outcome << '\n';
    logger::write("capture: %s", outcome.c_str());
    if (service) refw::invoke(calls::method(calls::kSaveCleanData), service);
    if (g_root) refw::release(g_root);
    g_root = nullptr;
    g_step = Step::Done;
}

struct Sections {
    void* contexts = nullptr;
    void* items = nullptr;
    size_t count = 0;    // sections in the root's list
    std::string types;   // their runtime types, for the log when the two are not among them
};

// The loaded root's two sections the character is read from (the list fills in after the load reports success).
Sections sections() {
    Sections out;
    void* list = refw::field<void*>(g_root, kGameSaveDataList);
    out.count = list ? save_sections::count(calls::kSaveDataListCount, list) : 0;
    for (size_t i = 0; i < out.count; ++i) {
        void* section = save_sections::item(calls::kSaveDataListItem, list, i);
        if (save_sections::isType(section, save_sections::kContextSection)) out.contexts = section;
        if (save_sections::isType(section, save_sections::kItemSection)) out.items = section;
        out.types += (out.types.empty() ? "" : ", ") + refw::typeNameOf(section);
    }
    return out;
}

void startLoad(void* service) {
    g_root = refw::createInstance(kGameSaveDataType);
    void* name = refw::managedString(kGameDataClass);
    if (!g_root || !name) return finish(service, "failed: the game's save classes are not available");
    refw::invoke(calls::method(calls::kSaveCleanData), service);
    const refw::InvokeResult* added = refw::invoke(calls::method(calls::kSaveAddDataClass), service, {name, g_root});
    const refw::InvokeResult* loading =
        added && added->byte ? refw::invoke(calls::method(calls::kSaveLoadSingle), service,
                                            {reinterpret_cast<void*>(static_cast<intptr_t>(g_slot))})
                             : nullptr;
    if (!loading || !loading->byte) return finish(service, "failed: the game refused to load save slot " + std::to_string(g_slot));
    g_step = Step::Loading;
}

// Once the load succeeded, waits for the root's list to hold both sections (kSectionsWaitMs), then captures.
void readLoaded(void* service, uint64_t nowMs) {
    const int64_t result = dword(calls::kGetLastSaveResult, service);
    if (result == *g_doing) return;
    if (result != *g_success) return finish(service, "failed: the save did not load (result " + std::to_string(result) + ")");
    if (!g_loadedMs) g_loadedMs = nowMs;
    const Sections found = sections();
    if (!found.contexts || !found.items) {
        if (nowMs - g_loadedMs < kSectionsWaitMs) return;
        logger::write("capture: %zu sections after the load: %s", found.count, found.types.c_str());
        return finish(service, "failed: the save holds no character (" + std::to_string(found.count) + " sections loaded)");
    }
    if (!profile::captureFrom(found.contexts, found.items)) return finish(service, "failed: the character could not be read from the save");
    finish(service, "ok");
}

}  // namespace

namespace offline_capture {

void enable(int slot) {
    g_slot = slot;
    g_idle = literal(kStateEnum, kIdleLiteral);
    g_success = literal(kResultEnum, kSuccessLiteral);
    g_doing = literal(kResultEnum, kDoingLiteral);
    g_step = Step::Waiting;
    logger::write("capture: armed for save slot %d (literals %s)", slot, g_idle && g_success && g_doing ? "found" : "MISSING");
}

void tick(uint64_t nowMs) {
    if (g_step == Step::Off || g_step == Step::Done) return;
    void* service = refw::nativeSingleton(kSaveService);
    if (!g_idle || !g_success || !g_doing || !calls::enabled(calls::Feature::Saves)) return finish(service, "failed: the save service is not available");
    if (!g_startMs) g_startMs = nowMs;
    if (nowMs - g_startMs > kTimeoutMs) return finish(service, "failed: the save did not load within 60 s");
    if (!service || dword(calls::kGetSaveState, service) != *g_idle) {
        g_titleSinceMs = 0;
        return;
    }
    if (g_step == Step::Waiting) {
        void* flow = refw::managedSingleton(kMainFlowManager);
        const refw::InvokeResult* title = flow ? refw::invoke(calls::method(calls::kIsTitleMenuPhase), flow) : nullptr;
        if (!title || !title->byte) {
            g_titleSinceMs = 0;
            return;
        }
        if (!g_titleSinceMs) g_titleSinceMs = nowMs;
        if (nowMs - g_titleSinceMs >= kTitleSettleMs) startLoad(service);
    } else if (g_step == Step::Loading) {
        readLoaded(service, nowMs);
    }
}

}  // namespace offline_capture
