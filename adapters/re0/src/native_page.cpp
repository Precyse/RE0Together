#include "native_page.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "crash_dump.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "native_menu.h"

namespace {

using native_menu_rule::Event;
using native_menu_rule::EventKind;
using native_menu_rule::kMaxRows;
using native_menu_rule::MenuDef;
using native_menu_rule::PageView;
using native_menu_rule::Reply;

using FastcallFunction = void(__fastcall*)(void* self, void* edx);
using CdeclBits = uint32_t(__cdecl*)();
using CdeclBool = bool(__cdecl*)();
using CdeclPosition = float*(__cdecl*)(float* out);
using CdeclSound = void(__cdecl*)(int32_t id);

constexpr size_t kTextBuffer = native_menu_rule::kMaxTextBytes + 1;
constexpr size_t kPositionFloats = 4;
constexpr uint32_t kHitTestPrecise = 1;

FastcallFunction g_originalRouter = nullptr;
FastcallFunction g_originalInput = nullptr;
FastcallFunction g_originalRefresh = nullptr;

// Game thread only, like everything below.
const MenuDef* g_pendingMenu = nullptr;  // the title is opening the Options screen for this menu
const MenuDef* g_menu = nullptr;         // the open page's menu
uintptr_t g_page = 0;                    // the borrowed PC Setup page while open
bool g_opened = false;                   // the Opened event was sent
bool g_closeRequested = false;
bool g_faulted = false;
PageView g_shown;                        // what the page's objects hold
bool g_shownValid = false;
int g_cursor = 0;
// setString keeps the pointer it gets: one buffer per object, rewritten only right before its own setString.
std::array<std::array<char, kTextBuffer>, kMaxRows> g_labelText{};
std::array<std::array<char, kTextBuffer>, kMaxRows> g_valueText{};
std::array<char, kTextBuffer> g_headerText{};

uintptr_t field(uintptr_t object, uintptr_t offset) { return game::readPointer(object + offset); }
uintptr_t rowField(uintptr_t offset, size_t row) { return field(g_page, offset + row * sizeof(uint32_t)); }
void* asPointer(uintptr_t address) { return reinterpret_cast<void*>(address); }

void setText(uintptr_t function, uintptr_t object, std::array<char, kTextBuffer>& buffer, const std::string& text) {
    if (!object) return;
    strncpy_s(buffer.data(), buffer.size(), text.c_str(), _TRUNCATE);
    game::callThiscall<void>(function, asPointer(object), static_cast<const char*>(buffer.data()));
}

void setVisible(uintptr_t instance, bool visible) {
    if (instance) game::callThiscall<void>(game::kInstanceSetVisibleFunction, asPointer(instance), static_cast<uint32_t>(visible));
}

void setSequence(uintptr_t instance, int32_t sequence) {
    if (instance) game::callThiscall<void>(game::kInstanceSetSequenceFunction, asPointer(instance), sequence);
}

void playSound(int32_t id) { reinterpret_cast<CdeclSound>(game::kPlayUiSoundFunction)(id); }

// The row's objects get its text and visibility; only what changed is written (a setString lays the glyphs out again).
void writeRow(size_t row, const native_menu_rule::RowView& view, bool force) {
    const native_menu_rule::RowView& shown = g_shown.rows[row];
    const bool visible = row < g_menu->count && view.visible;
    if (force || visible != (row < g_menu->count && shown.visible)) {
        setVisible(rowField(game::kPcSetupRowsOffset, row), visible);
        setVisible(rowField(game::kPcSetupValuesOffset, row), visible);
    }
    if (force || view.label != shown.label) {
        setText(game::kObjectSetStringFunction, rowField(game::kPcSetupLabelObjectsOffset, row), g_labelText[row], view.label);
    }
    if (force || view.value != shown.value) {
        setText(game::kObjectSetStringFunction, rowField(game::kPcSetupValueObjectsOffset, row), g_valueText[row], view.value);
    }
}

// The arrows on the cursor row when it is a choice that may change, and the cursor row highlighted (vanilla 0x5c7020).
void placeCursor(const PageView& view) {
    game::writeMemory(g_page + game::kPcSetupCursorOffset, static_cast<uint32_t>(g_cursor));
    float pitch = 0.0f;
    game::readMemory(game::kPcSetupRowPitchGlobal, pitch);
    const uintptr_t arrows = field(g_page, game::kPcSetupArrowsOffset);
    if (arrows) game::callThiscall<void>(game::kNullSetPositionYFunction, asPointer(arrows), static_cast<float>(g_cursor) * pitch);
    setVisible(arrows, native_menu_rule::arrowsShown(*g_menu, view, g_cursor));
    for (size_t row = 0; row < kMaxRows; ++row) {
        const int32_t sequence = static_cast<int>(row) == g_cursor ? game::kSequenceHighlight : game::kSequenceNormal;
        setSequence(rowField(game::kPcSetupRowsOffset, row), sequence);
        setSequence(rowField(game::kPcSetupValuesOffset, row), sequence);
    }
}

// Writes the menu's view into the page; `force` rewrites every object (the page's first frame).
void show(const PageView& view, bool force) {
    if (force) setVisible(field(g_page, game::kPcSetupTitleOffset), false);
    if (force || view.header != g_shown.header) {
        setText(game::kInstanceSetStringFunction, field(g_page, game::kPcSetupHelpOffset), g_headerText, view.header);
    }
    for (size_t row = 0; row < kMaxRows; ++row) writeRow(row, view.rows[row], force);
    g_cursor = native_menu_rule::keepCursor(*g_menu, view, g_cursor);
    placeCursor(view);
    g_shown = view;
    g_shownValid = true;
}

bool loaded() {
    uint8_t value = 0;
    return game::readMemory(g_page + game::kGuiLoadedOffset, value) && value != 0;
}

// What the vanilla decide / cancel does (0x5c6be9): the sound, nothing to apply, fade out, state 4; states 4 and 5
// then close the page and the router hook sees it closed.
void startClose(int32_t sound) {
    if (sound) playSound(sound);
    game::writeMemory(g_page + game::kPcSetupDecidedOffset, uint16_t{0});
    const uintptr_t fade = game::fadeObject();
    if (fade) game::callThiscall<void>(game::kFadeStartFunction, asPointer(fade), game::kMenuFadeSeconds);
    game::writeMemory(g_page + game::kPcSetupStateOffset, game::kPcSetupStateClosing);
}

bool hitTest(uintptr_t instance, uintptr_t object, float* position) {
    return instance && object &&
           game::callThiscall<bool>(game::kGuiHitTestFunction, asPointer(g_page), instance, object, position, kHitTestPrecise);
}

uint32_t padMask(uintptr_t offset) {
    const uintptr_t pad = game::readPointer(game::kGamePadGlobal);
    uint32_t mask = 0;
    if (pad) game::readMemory(pad + offset, mask);
    return mask;
}

bool subMenuClosing() {
    const uintptr_t subMenu = game::readPointer(game::kSubMenuGlobal);
    uint8_t closing = 0;
    return subMenu && game::readMemory(subMenu + game::kSubMenuCloseRequestOffset, closing) && closing == 1;
}

// The page's input read as 0x5c6540 reads it: trigger and repeat bits with the key guide's clicks, then the mouse.
native_menu_rule::Input readInput(const PageView& view) {
    uint32_t trigger = reinterpret_cast<CdeclBits>(game::kMenuTriggerFunction)();
    uint32_t repeat = reinterpret_cast<CdeclBits>(game::kMenuRepeatFunction)();
    const uintptr_t manager = field(g_page, game::kPcSetupManagerOffset);
    if (manager) game::callThiscall<void>(game::kOptionMouseInputFunction, asPointer(manager), &trigger, &repeat);
    native_menu_rule::Input input;
    input.cancel = subMenuClosing() || (trigger & padMask(game::kPadCancelMaskOffset)) != 0;
    input.decide = (trigger & padMask(game::kPadDecideMaskOffset)) != 0;
    input.up = (repeat & game::kRepeatUp) != 0;
    input.down = (repeat & game::kRepeatDown) != 0;
    input.left = (repeat & game::kRepeatLeft) != 0;
    input.right = (repeat & game::kRepeatRight) != 0;
    if (!reinterpret_cast<CdeclBool>(game::kMouseClickedFunction)()) return input;
    float position[kPositionFloats] = {};
    reinterpret_cast<CdeclPosition>(game::kMousePositionFunction)(position);
    if (native_menu_rule::arrowsShown(*g_menu, view, g_cursor)) {
        if (hitTest(field(g_page, game::kPcSetupArrowLeftOffset), field(g_page, game::kPcSetupArrowLeftTouchOffset), position)) {
            input.clickedArrow = -1;
        } else if (hitTest(field(g_page, game::kPcSetupArrowRightOffset), field(g_page, game::kPcSetupArrowRightTouchOffset), position)) {
            input.clickedArrow = 1;
        }
    }
    for (size_t row = 0; input.clickedArrow == 0 && row < g_menu->count; ++row) {
        if (view.rows[row].visible &&
            hitTest(rowField(game::kPcSetupRowsOffset, row), rowField(game::kPcSetupRowTouchOffset, row), position)) {
            input.clickedRow = static_cast<int>(row);
        }
    }
    return input;
}

// The vanilla arrow flash: a pressed arrow plays its highlight once, then goes back to normal.
void settleArrows() {
    for (const uintptr_t offset : {game::kPcSetupArrowLeftOffset, game::kPcSetupArrowRightOffset}) {
        const uintptr_t arrow = field(g_page, offset);
        if (arrow && game::callThiscall<bool>(game::kGuiSequenceDoneFunction, asPointer(g_page), arrow, game::kSequenceHighlight)) {
            setSequence(arrow, game::kSequenceNormal);
        }
    }
}

void handle(const Event& event, const PageView& view) {
    if (event.kind == EventKind::Select) {
        if (event.row == g_cursor) return;
        playSound(game::kSoundMove);
        g_cursor = event.row;
        placeCursor(view);
        return;
    }
    const Reply reply = native_menu::dispatch(*g_menu, view, event);
    if (event.kind == EventKind::Cancel) {
        if (reply != Reply::Reject) startClose(game::kSoundCancel);
        return;
    }
    if (reply == Reply::Reject) {
        playSound(game::kSoundCancel);
        return;
    }
    if (reply == Reply::Close) {
        startClose(game::kSoundDecide);
        return;
    }
    if (event.kind == EventKind::Change) {
        playSound(game::kSoundMove);
        setSequence(field(g_page, event.step < 0 ? game::kPcSetupArrowLeftOffset : game::kPcSetupArrowRightOffset),
                    game::kSequenceHighlight);
    } else {
        playSound(game::kSoundDecide);
    }
    show(native_menu::view(*g_menu), false);
}

// State 3 of the borrowed page: our menu instead of the PC settings.
void runInput() {
    if (!loaded()) return;
    settleArrows();
    const PageView view = native_menu::view(*g_menu);
    show(view, !g_shownValid);
    if (!g_opened) {
        g_opened = true;
        native_menu::dispatch(*g_menu, view, Event{EventKind::Opened});
        logger::write("native_page: menu %.*s open", static_cast<int>(g_menu->id.size()), g_menu->id.data());
    }
    if (g_closeRequested) {
        g_closeRequested = false;
        startClose(0);
        return;
    }
    if (const auto event = native_menu_rule::decode(*g_menu, g_shown, g_cursor, readInput(g_shown))) handle(*event, g_shown);
}

// The page's open, as uGUIOptionPcSetup::open 0x5c6fe0 does it without capturing the video settings.
void openPage(uintptr_t manager, uintptr_t page) {
    for (const uintptr_t offset : {game::kOptionKeyGuideOffset, game::kOptionBlackOffset, game::kOptionFrameOffset}) {
        const uintptr_t unit = field(manager, offset);
        uint32_t flags = 0;
        if (unit && game::readMemory(unit + game::kUnitFlagsOffset, flags)) {
            game::writeMemory(unit + game::kUnitFlagsOffset, flags | game::kUnitDrawFlag);
        }
    }
    game::callThiscall<void>(game::kOptionKeyGuideFunction, asPointer(manager), uint32_t{0});
    g_menu = g_pendingMenu;
    g_pendingMenu = nullptr;
    g_page = page;
    g_opened = false;
    g_shownValid = false;
    g_cursor = native_menu_rule::firstCursor(*g_menu, native_menu::view(*g_menu));
    game::writeMemory(page + game::kPcSetupCursorOffset, static_cast<uint32_t>(g_cursor));
    game::writeMemory(page + game::kPcSetupDecidedOffset, uint16_t{0});
    game::writeMemory(page + game::kPcSetupStateOffset, game::kPcSetupStateOpen);
    game::writeMemory(manager + game::kOptionStateOffset, game::kOptionStateParked);
    logger::write("native_page: opening menu %.*s on the PC Setup page", static_cast<int>(g_menu->id.size()), g_menu->id.data());
}

// The page is gone. `closed`: it closed itself (state 0), so the whole Options screen closes and the queued work runs;
// else the game moved the router and nothing of ours is written.
void finish(uintptr_t manager, bool closed) {
    const MenuDef* menu = g_menu;
    g_menu = nullptr;
    g_page = 0;
    g_closeRequested = false;
    if (closed) {
        native_menu::dispatch(*menu, g_shown, Event{EventKind::Closed});
        game::writeMemory(manager + game::kOptionStateOffset, game::kOptionStateCloseAll);
        logger::write("native_page: menu %.*s closed", static_cast<int>(menu->id.size()), menu->id.data());
        native_menu::runAfterClose();
    } else {
        native_menu::dropAfterClose();
        logger::write("native_page: the Options screen moved on, menu %.*s dropped", static_cast<int>(menu->id.size()), menu->id.data());
    }
}

void routerStep(uintptr_t manager) {
    uint32_t state = 0;
    if (!game::readMemory(manager + game::kOptionStateOffset, state)) return;
    if (g_menu) {
        uint32_t pageState = 0;
        if (state != game::kOptionStateParked) finish(manager, false);
        else if (game::readMemory(g_page + game::kPcSetupStateOffset, pageState) && pageState == 0) finish(manager, true);
        return;
    }
    if (!g_pendingMenu || state == 0) return;
    if (state != game::kOptionStateOpening) {
        g_pendingMenu = nullptr;
        g_closeRequested = false;  // the router got past its opening without us: the Options list is the game's
        logger::write("native_page: the Options screen opened its list (state %u), menu not shown", state);
        return;
    }
    uint8_t creating = 0;
    const uintptr_t page = field(manager, game::kOptionPcSetupOffset);
    if (!game::readMemory(manager + game::kOptionCreatingOffset, creating) || creating || !page || !game::fadeDone()) return;
    openPage(manager, page);
}

// A fault in our code closes the whole Options screen and switches the page off for the session.
void fault(uintptr_t manager) {
    g_faulted = true;
    g_pendingMenu = nullptr;
    if (!g_menu) return;
    g_menu = nullptr;
    g_page = 0;
    native_menu::dropAfterClose();
    if (manager) game::writeMemory(manager + game::kOptionStateOffset, game::kOptionStateCloseAll);
}

void __fastcall routerDetour(void* self, void* edx) {
    if (!g_faulted) {
        __try {
            routerStep(reinterpret_cast<uintptr_t>(self));
        } __except (crash_dump::reportCaught(GetExceptionInformation(), "native_page: router")) {
            fault(reinterpret_cast<uintptr_t>(self));
        }
    }
    g_originalRouter(self, edx);
}

bool ours(void* page) { return !g_faulted && g_menu && reinterpret_cast<uintptr_t>(page) == g_page; }

void guardedPageStep(void (*step)(), void* page) {
    __try {
        step();
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "native_page: page")) {
        fault(field(reinterpret_cast<uintptr_t>(page), game::kPcSetupManagerOffset));
    }
}

void __fastcall inputDetour(void* self, void* edx) {
    if (ours(self)) guardedPageStep(runInput, self);
    else g_originalInput(self, edx);
}

void refreshOurs() {
    if (loaded()) show(native_menu::view(*g_menu), true);
}

void __fastcall refreshDetour(void* self, void* edx) {
    if (ours(self)) guardedPageStep(refreshOurs, self);
    else g_originalRefresh(self, edx);
}

bool sitesMatch() {
    return game::bytesMatch(game::kOptionManagerMoveFunction, game::kOptionManagerMoveBytes) &&
           game::bytesMatch(game::kOptionRouterJumpCheck, game::kOptionRouterJumpBytes) &&
           game::bytesMatch(game::kPcSetupInputFunction, game::kPcSetupInputBytes) &&
           game::bytesMatch(game::kPcSetupRefreshFunction, game::kPcSetupRefreshBytes);
}

}  // namespace

namespace native_page {

bool enable() {
    if (!sitesMatch()) {
        logger::write("native_page: the Options screen is not the expected code, native menus off");
        return false;
    }
    return hooks::install("uOptionManager::v8", game::kOptionManagerMoveFunction, reinterpret_cast<void*>(&routerDetour),
                          reinterpret_cast<void**>(&g_originalRouter)) &&
           hooks::install("uGUIOptionPcSetup input", game::kPcSetupInputFunction, reinterpret_cast<void*>(&inputDetour),
                          reinterpret_cast<void**>(&g_originalInput)) &&
           hooks::install("uGUIOptionPcSetup refresh", game::kPcSetupRefreshFunction, reinterpret_cast<void*>(&refreshDetour),
                          reinterpret_cast<void**>(&g_originalRefresh));
}

bool openFromTitle(std::string_view menuId) {
    if (g_faulted || !g_originalRouter || g_menu) return false;
    g_pendingMenu = native_menu::find(menuId);
    return g_pendingMenu != nullptr;
}

void requestClose() {
    if (g_menu || g_pendingMenu) g_closeRequested = true;
}

bool active() { return g_menu || g_pendingMenu; }

void uninstall() {
    hooks::remove(game::kPcSetupRefreshFunction);
    hooks::remove(game::kPcSetupInputFunction);
    hooks::remove(game::kOptionManagerMoveFunction);
}

}  // namespace native_page
