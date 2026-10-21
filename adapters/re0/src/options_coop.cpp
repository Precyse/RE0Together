#include "options_coop.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "coop_page.h"
#include "crash_dump.h"
#include "game.h"
#include "gui_text_rule.h"
#include "hooks.h"
#include "log.h"
#include "options_coop_rule.h"

namespace {

using options_coop_rule::Items;
using options_coop_rule::RouterView;

// The first instructions of the three functions and the two router sites this relies on, as read from the image.
constexpr std::array<uint8_t, 4> kTopBuildBytes = {0x56, 0x8b, 0xf1, 0xe8};  // push esi; mov esi, ecx; call
constexpr std::array<uint8_t, 10> kRouterBytes = {0x83, 0xec, 0x40, 0x53, 0x8b, 0xd9, 0x56, 0x8d, 0x73, 0x30};
constexpr std::array<uint8_t, 6> kRouterJumpBytes = {0x48, 0x83, 0xf8, 0x15, 0x0f, 0x87};  // dec eax; cmp eax, 0x15; ja
constexpr std::array<uint8_t, 7> kRouterDefaultBytes = {0x56, 0xff, 0x15, 0x5c, 0x11, 0xcb, 0x00};  // unlock, return
constexpr std::array<uint8_t, 4> kNoHelpBytes = {0xff, 0xff, 0xff, 0xff};  // the unused item's help entry (-1)
constexpr uint32_t kNoHelpId = 0xffffffffu;
constexpr uintptr_t kCoopHelpEntry = game::kOptionHelpTable + options_coop_rule::kCoopItem * sizeof(uint32_t);

// uGUIOptionTop's fields from +0x264, copied in one read.
struct TopFields {
    uint8_t cancelled;
    uint8_t padding[3];
    uint32_t state;
    Items items;
    uint32_t count;
    uint32_t scroll;
    uint32_t cursor;
};
static_assert(offsetof(TopFields, state) == game::kOptionTopStateOffset - game::kOptionTopFieldsOffset);
static_assert(offsetof(TopFields, items) == game::kOptionTopItemsOffset - game::kOptionTopFieldsOffset);
static_assert(offsetof(TopFields, count) == game::kOptionTopCountOffset - game::kOptionTopFieldsOffset);
static_assert(offsetof(TopFields, scroll) == game::kOptionTopScrollOffset - game::kOptionTopFieldsOffset);
static_assert(offsetof(TopFields, cursor) == game::kOptionTopCursorOffset - game::kOptionTopFieldsOffset);
static_assert(sizeof(TopFields) == game::kOptionTopFieldsSize);
static_assert(sizeof(Items) == game::kOptionTopCountOffset - game::kOptionTopItemsOffset);

using FastcallFunction = void(__fastcall*)(void* self, void* edx);

FastcallFunction g_originalTopBuild = nullptr;
FastcallFunction g_originalRouter = nullptr;
bool g_pageOpen = false;  // game thread only, like everything below
bool g_faulted = false;   // a fault in our router step switched the entry off for the session

uintptr_t topOf(uintptr_t manager) { return game::readPointer(manager + game::kOptionTopOffset); }

bool readRouterView(uintptr_t manager, uint32_t state, RouterView& view) {
    uint8_t creating = 0;
    uint8_t dialog = 0;
    const uintptr_t top = topOf(manager);
    TopFields fields{};
    if (!top || !game::readMemory(manager + game::kOptionCreatingOffset, creating) ||
        !game::readMemory(manager + game::kOptionDialogOffset, dialog) ||
        !game::readMemory(top + game::kOptionTopFieldsOffset, fields)) {
        return false;
    }
    view = {state, creating != 0, dialog != 0, fields.state,
            options_coop_rule::selectedItem(fields.cancelled != 0, fields.items, fields.count, fields.scroll, fields.cursor)};
    return true;
}

void openPage(uintptr_t manager) {
    game::writeMemory(manager + game::kOptionStateOffset, options_coop_rule::kStateParked);
    game::callThiscall<void>(game::kOptionKeyGuideFunction, reinterpret_cast<void*>(manager), 0);  // as for every page
    coop_page::open();
    g_pageOpen = true;
}

// `showTopAgain` is false when the game moved the router itself: its state is then not ours to set.
void closePage(uintptr_t manager, bool showTopAgain) {
    g_pageOpen = false;
    coop_page::close();
    if (showTopAgain) game::writeMemory(manager + game::kOptionStateOffset, options_coop_rule::kStateReopenTop);
}

void routerStep(uintptr_t manager) {
    uint32_t state = 0;
    if (!game::readMemory(manager + game::kOptionStateOffset, state)) return;
    if (g_pageOpen) {
        if (!options_coop_rule::stillParked(state)) closePage(manager, false);
        else if (coop_page::update()) closePage(manager, true);
        return;
    }
    RouterView view;
    if (state == options_coop_rule::kStateTopWait && readRouterView(manager, state, view) &&
        options_coop_rule::opensCoopPage(view)) {
        openPage(manager);
    }
}

void guardedRouterStep(uintptr_t manager) {
    __try {
        routerStep(manager);
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "options_coop: router step")) {
        g_faulted = true;
        if (g_pageOpen) closePage(manager, true);
    }
}

void __fastcall routerDetour(void* self, void* edx) {
    if (!g_faulted) guardedRouterStep(reinterpret_cast<uintptr_t>(self));
    g_originalRouter(self, edx);
}

// Runs after the game filled the list: Co-op goes in before Back.
void __fastcall topBuildDetour(void* self, void* edx) {
    g_originalTopBuild(self, edx);
    const auto top = reinterpret_cast<uintptr_t>(self);
    TopFields fields{};
    if (!game::readMemory(top + game::kOptionTopFieldsOffset, fields)) return;
    const uint32_t count = options_coop_rule::withCoopItem(fields.items, fields.count);
    if (count == fields.count) return;
    game::writeMemory(top + game::kOptionTopItemsOffset, fields.items);
    game::writeMemory(top + game::kOptionTopCountOffset, count);
}

bool sitesMatch() {
    return game::bytesMatch(game::kOptionTopBuildFunction, kTopBuildBytes) &&
           game::bytesMatch(game::kOptionManagerMoveFunction, kRouterBytes) &&
           game::bytesMatch(game::kOptionRouterJumpCheck, kRouterJumpBytes) &&
           game::bytesMatch(game::kOptionRouterDefault, kRouterDefaultBytes) &&
           game::bytesMatch(kCoopHelpEntry, kNoHelpBytes);
}

}  // namespace

namespace options_coop {

bool enable() {
    if (!sitesMatch()) {
        logger::write("options_coop: the Options screen is not the expected code, entry not added");
        return false;
    }
    return game::writeProtected(kCoopHelpEntry, gui_text_rule::kCoopHelpId) &&
           hooks::install("uOptionManager::v8", game::kOptionManagerMoveFunction, reinterpret_cast<void*>(&routerDetour),
                          reinterpret_cast<void**>(&g_originalRouter)) &&
           hooks::install("uGUIOptionTop::v5", game::kOptionTopBuildFunction, reinterpret_cast<void*>(&topBuildDetour),
                          reinterpret_cast<void**>(&g_originalTopBuild));
}

void uninstall() {
    hooks::remove(game::kOptionTopBuildFunction);
    hooks::remove(game::kOptionManagerMoveFunction);
    game::writeProtected(kCoopHelpEntry, kNoHelpId);
}

}  // namespace options_coop
