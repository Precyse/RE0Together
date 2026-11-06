#include "title_menu.h"

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

#include "calls.h"
#include "hook_registry.h"
#include "log.h"
#include "names.h"
#include "refw.h"
#include "save_redirect.h"
#include "save_service.h"
#include "title_menu_rule.h"

namespace {

// REFramework hook arguments: argv[0] the VM context, argv[1] `this`.
constexpr int kThisParam = 1;

constexpr const char* kGuiType = "app.ui030201";
constexpr const char* kInfoType = "app.ui030201.ListInfo";
constexpr const char* kListField = "ListCtrl";
constexpr const char* kFlowField = "FlowNow";
constexpr const char* kItemTypeField = "ItemType";
constexpr const char* kFlowEnum = "app.ui030201.FlowId";
constexpr const char* kSelectMenuLiteral = "SelectMenu";
constexpr const char* kItemEnum = "app.ui030201.ListItemType";
constexpr const char* kLatestLiteral = "Latest";
constexpr const char* kNewGameLiteral = "NewGame";
constexpr const char* kInitGameLiteral = "InitGame";

bool g_guest = false;
bool g_autoLoad = false;
bool g_pressed = false;  // Continue was pressed (or there was no row for it): auto_load is done
uint64_t g_waitingSinceMs = 0;

// An enum literal as the menu compares it.
int32_t literal(const char* type, const char* name) {
    const std::optional<int64_t> value = refw::enumLiteral(type, name);
    return value ? static_cast<int32_t>(*value) : title_menu_rule::kUnknownLiteral;
}

int32_t selectMenu() {
    static const int32_t value = literal(kFlowEnum, kSelectMenuLiteral);
    return value;
}

int32_t latest() {
    static const int32_t value = literal(kItemEnum, kLatestLiteral);
    return value;
}

int32_t newGame() {
    static const int32_t value = literal(kItemEnum, kNewGameLiteral);
    return value;
}

int32_t initGame() {
    static const int32_t value = literal(kItemEnum, kInitGameLiteral);
    return value;
}

void* listOf(void* menu) {
    void* list = nullptr;
    return refw::instanceField(kGuiType, kListField, menu, list) ? list : nullptr;
}

int32_t itemTypeOf(void* info) {
    int32_t type = title_menu_rule::kUnknownLiteral;
    return refw::instanceField(kInfoType, kItemTypeField, info, type) ? type : title_menu_rule::kUnknownLiteral;
}

void* intArg(int32_t value) { return reinterpret_cast<void*>(static_cast<intptr_t>(value)); }

bool menuWaiting(void* menu) {
    int32_t flow = title_menu_rule::kUnknownLiteral;
    if (!refw::instanceField(kGuiType, kFlowField, menu, flow) || selectMenu() == title_menu_rule::kUnknownLiteral) return false;
    const title_menu_rule::Menu state{flow == selectMenu(), save_service::idle(save_service::instance()), save_redirect::safeToLoad()};
    return title_menu_rule::waiting(state);
}

// The ItemType of every row, in the menu's order.
std::vector<int32_t> rowTypes(void* list) {
    std::vector<int32_t> types;
    const refw::InvokeResult* count = refw::invoke(calls::method(calls::kTitleListCount), list);
    const int32_t rows = count ? static_cast<int32_t>(count->dword) : 0;
    for (int32_t i = 0; i < rows; ++i) {
        const refw::InvokeResult* info = refw::invoke(calls::method(calls::kTitleListInfo), list, {intArg(i)});
        types.push_back(info && info->ptr ? itemTypeOf(info->ptr) : title_menu_rule::kUnknownLiteral);
    }
    return types;
}

std::string describe(const std::vector<int32_t>& types) {
    std::string text;
    for (const int32_t type : types) text += (text.empty() ? "" : ",") + std::to_string(type);
    return text;
}

// Selects the Continue row and runs the menu's own selection handler on it; once.
void pressContinue(void* menu) {
    g_pressed = true;
    void* list = listOf(menu);
    const std::vector<int32_t> types = list ? rowTypes(list) : std::vector<int32_t>{};
    const int row = title_menu_rule::rowOf(types, latest());
    if (row < 0) {
        logger::write("title_menu: auto_load: no Continue row (%zu rows, item types %s)", types.size(), describe(types).c_str());
        return;
    }
    refw::invoke(calls::method(calls::kTitleListSelect), list, {intArg(row), nullptr});
    refw::invoke(calls::method(calls::kTitleSelectListCall), menu);
    logger::write("title_menu: auto_load: Continue pressed (row %d of %zu, item types %s)", row, types.size(), describe(types).c_str());
}

// Every frame the menu is up, before its own update.
int preUpdate(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    if (!g_autoLoad || g_pressed || argc <= kThisParam) return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    void* menu = argv[kThisParam];
    const uint64_t nowMs = GetTickCount64();
    if (!menuWaiting(menu)) {
        g_waitingSinceMs = 0;
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }
    if (!g_waitingSinceMs) g_waitingSinceMs = nowMs;
    if (title_menu_rule::settled(g_waitingSinceMs, nowMs)) pressContinue(menu);
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

// A row is chosen: a guest's New Game rows are refused.
int preSelectList(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    if (!g_guest || argc <= kThisParam) return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    void* list = listOf(argv[kThisParam]);
    const refw::InvokeResult* selected = list ? refw::invoke(calls::method(calls::kTitleListSelected), list) : nullptr;
    const int32_t type = selected && selected->ptr ? itemTypeOf(selected->ptr) : title_menu_rule::kUnknownLiteral;
    if (!title_menu_rule::blocksNewGame(g_guest, type, newGame(), initGame())) return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    logger::write("title_menu: New Game refused (item type %d): a guest plays the host's world", type);
    return REFRAMEWORK_HOOK_SKIP_ORIGINAL;
}

}  // namespace

namespace title_menu {

bool install(bool guest, bool autoLoad) {
    if (!calls::enabled(calls::Feature::Title)) {
        logger::write("title_menu: off (title calls missing)");
        return false;
    }
    g_guest = guest;
    g_autoLoad = autoLoad;
    const hook_registry::Handler handlers[] = {hook_registry::make<preUpdate>(&names::kTitleUpdate),
                                               hook_registry::make<preSelectList>(&names::kTitleSelectList)};
    return hook_registry::installSet(names::kTitleMenu, handlers);
}

}  // namespace title_menu
