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
#include "save_redirect.h"
#include "save_sections.h"
#include "save_service.h"

namespace {

constexpr const char* kResultEnum = "via.storage.saveService.SaveResult";
constexpr const char* kSuccessLiteral = "Success";
constexpr const char* kDoingLiteral = "Doing";
constexpr const char* kGameSaveDataType = "app.SaveDataManager.GameSaveData";
constexpr const wchar_t* kGameDataClass = L"GAME_SAVE_DATA";  // the SaveService's name for a game save's root class
constexpr uint32_t kGameSaveDataList = 0x10;                   // GameSaveData.GameSaveDataList, List<app.SaveDataBase>
constexpr uint64_t kTitleWaitMs = 300000;     // how long the game may take to reach its title menu
constexpr uint64_t kTimeoutMs = 60000;        // from the title menu first shown to the character written
constexpr uint64_t kTitleSettleMs = 3000;     // the title menu shown this long with the SaveService idle: the game's own loads are done
constexpr uint64_t kSectionsWaitMs = 10000;   // how long the root's list may take to fill after the load succeeded
constexpr const char* kMainFlowManager = "app.MainFlowManager";
constexpr const wchar_t* kResultFile = L"\\capture.result";

// The codes of `failed: <code>: <detail>` in the result file (docs/CONTRACT.md): where the capture stopped.
constexpr const char* kFailTitle = "title";        // the title menu was never reached
constexpr const char* kFailLoad = "load";          // the game could not or would not load the slot
constexpr const char* kFailEmpty = "empty";        // the slot loaded and holds no character
constexpr const char* kFailRedirect = "redirect";  // the save folder redirect was refused
constexpr const char* kFailWrite = "write";        // the character was not readable from the save or not written
constexpr const char* kFailTimeout = "timeout";    // the load did not finish in time

enum class Step { Off, Waiting, Loading, Done };

Step g_step = Step::Off;
int g_slot = -1;
uint64_t g_armedMs = 0;      // the first step after enable
uint64_t g_titleMs = 0;      // when the title menu was first seen; 0 before
uint64_t g_idleSinceMs = 0;  // when the title menu was first seen with the SaveService idle; 0 while it is not
uint64_t g_loadedMs = 0;     // when the load reported success; 0 before
void* g_root = nullptr;      // the scratch GameSaveData, one reference of ours
std::optional<int64_t> g_success, g_doing;

std::string failure(const char* code, const std::string& detail) { return std::string("failed: ") + code + ": " + detail; }

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

bool onTitleMenu() {
    void* flow = refw::managedSingleton(kMainFlowManager);
    const refw::InvokeResult* title = flow ? refw::invoke(calls::method(calls::kIsTitleMenuPhase), flow) : nullptr;
    return title && title->byte;
}

// Loads the slot into a scratch root. Waits while the save folder redirect is still being set; a refused one ends the capture
// (the slot would be read from the real save folder, not the world the launcher picked).
void startLoad(void* service) {
    const save_redirect::State redirect = save_redirect::state();
    if (redirect == save_redirect::State::Pending) return;
    if (redirect == save_redirect::State::Refused) return finish(service, failure(kFailRedirect, "the save folder redirect was refused"));
    g_root = refw::createInstance(kGameSaveDataType);
    void* name = refw::managedString(kGameDataClass);
    if (!g_root || !name) return finish(service, failure(kFailLoad, "the game's save classes are not available"));
    refw::invoke(calls::method(calls::kSaveCleanData), service);
    const refw::InvokeResult* added = refw::invoke(calls::method(calls::kSaveAddDataClass), service, {name, g_root});
    const refw::InvokeResult* loading =
        added && added->byte ? refw::invoke(calls::method(calls::kSaveLoadSingle), service,
                                            {reinterpret_cast<void*>(static_cast<intptr_t>(g_slot))})
                             : nullptr;
    if (!loading || !loading->byte) return finish(service, failure(kFailLoad, "the game refused to load save slot " + std::to_string(g_slot)));
    g_step = Step::Loading;
}

// Once the load succeeded, waits for the root's list to hold both sections (kSectionsWaitMs), then captures.
void readLoaded(void* service, uint64_t nowMs) {
    const int64_t result = dword(calls::kGetLastSaveResult, service);
    if (result == *g_doing) return;
    if (result != *g_success) return finish(service, failure(kFailLoad, "the save did not load (result " + std::to_string(result) + ")"));
    if (!g_loadedMs) g_loadedMs = nowMs;
    const Sections found = sections();
    if (!found.contexts || !found.items) {
        if (nowMs - g_loadedMs < kSectionsWaitMs) return;
        logger::write("capture: %zu sections after the load: %s", found.count, found.types.c_str());
        return finish(service, failure(kFailEmpty, "the save holds no character (" + std::to_string(found.count) + " sections loaded)"));
    }
    if (!profile::captureFrom(found.contexts, found.items)) return finish(service, failure(kFailWrite, "the character could not be read from the save or written"));
    finish(service, "ok");
}

}  // namespace

namespace offline_capture {

void enable(int slot) {
    g_slot = slot;
    g_success = refw::enumLiteral(kResultEnum, kSuccessLiteral);
    g_doing = refw::enumLiteral(kResultEnum, kDoingLiteral);
    g_step = Step::Waiting;
    logger::write("capture: armed for save slot %d (literals %s)", slot,
                  save_service::idleKnown() && g_success && g_doing ? "found" : "MISSING");
}

void tick(uint64_t nowMs) {
    if (g_step == Step::Off || g_step == Step::Done) return;
    void* service = save_service::instance();
    if (!save_service::idleKnown() || !g_success || !g_doing || !calls::enabled(calls::Feature::Saves)) {
        return finish(service, failure(kFailLoad, "the save service is not available"));
    }
    if (!g_armedMs) g_armedMs = nowMs;
    const bool title = onTitleMenu();
    if (title && !g_titleMs) g_titleMs = nowMs;
    if (!g_titleMs) {
        if (nowMs - g_armedMs > kTitleWaitMs) finish(service, failure(kFailTitle, "the title menu did not appear"));
        return;
    }
    if (nowMs - g_titleMs > kTimeoutMs) return finish(service, failure(kFailTimeout, "the save did not load within 60 s of the title"));
    if (!save_service::idle(service)) {
        g_idleSinceMs = 0;
        return;
    }
    if (g_step == Step::Loading) return readLoaded(service, nowMs);
    if (!title) {
        g_idleSinceMs = 0;
        return;
    }
    if (!g_idleSinceMs) g_idleSinceMs = nowMs;
    if (nowMs - g_idleSinceMs >= kTitleSettleMs) startLoad(service);
}

}  // namespace offline_capture
