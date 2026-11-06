#include "title_menu.h"

#include <optional>

#include "calls.h"
#include "hook_registry.h"
#include "log.h"
#include "names.h"
#include "refw.h"
#include "title_menu_rule.h"

namespace {

// REFramework hook arguments: argv[0] the VM context, argv[1] `this`.
constexpr int kThisParam = 1;

constexpr const char* kGuiType = "app.ui030201";
constexpr const char* kInfoType = "app.ui030201.ListInfo";
constexpr const char* kListField = "ListCtrl";
constexpr const char* kItemTypeField = "ItemType";
constexpr const char* kItemEnum = "app.ui030201.ListItemType";
constexpr const char* kNewGameLiteral = "NewGame";
constexpr const char* kInitGameLiteral = "InitGame";

// An enum literal as the menu compares it.
int32_t literal(const char* name) {
    const std::optional<int64_t> value = refw::enumLiteral(kItemEnum, name);
    return value ? static_cast<int32_t>(*value) : title_menu_rule::kUnknownLiteral;
}

void* listOf(void* menu) {
    void* list = nullptr;
    return refw::instanceField(kGuiType, kListField, menu, list) ? list : nullptr;
}

int32_t itemTypeOf(void* info) {
    int32_t type = title_menu_rule::kUnknownLiteral;
    return refw::instanceField(kInfoType, kItemTypeField, info, type) ? type : title_menu_rule::kUnknownLiteral;
}

// A row is chosen: the New Game rows are refused.
int preSelectList(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    if (argc <= kThisParam) return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    static const int32_t newGame = literal(kNewGameLiteral);
    static const int32_t initGame = literal(kInitGameLiteral);
    void* list = listOf(argv[kThisParam]);
    const refw::InvokeResult* selected = list ? refw::invoke(calls::method(calls::kTitleListSelected), list) : nullptr;
    const int32_t type = selected && selected->ptr ? itemTypeOf(selected->ptr) : title_menu_rule::kUnknownLiteral;
    if (!title_menu_rule::blocksNewGame(true, type, newGame, initGame)) return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    logger::write("title_menu: New Game refused (item type %d): a guest plays the host's world", type);
    return REFRAMEWORK_HOOK_SKIP_ORIGINAL;
}

}  // namespace

namespace title_menu {

bool install() {
    if (!calls::enabled(calls::Feature::Title)) {
        logger::write("title_menu: off (title calls missing)");
        return false;
    }
    const hook_registry::Handler handlers[] = {hook_registry::make<preSelectList>(&names::kTitleSelectList)};
    return hook_registry::installSet(names::kTitleMenu, handlers);
}

}  // namespace title_menu
