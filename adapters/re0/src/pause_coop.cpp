#include "pause_coop.h"

#include <windows.h>

#include "coop_menu_rule.h"
#include "crash_dump.h"
#include "debug_overlay.h"
#include "game.h"
#include "gui_input.h"
#include "live_menu_rule.h"
#include "log.h"
#include "native_page.h"
#include "pause_coop_rule.h"

namespace {

namespace rule = pause_coop_rule;

using GuiStep = void(__fastcall*)(void* gui, void* edx);

constexpr char kCoopLabel[] = "<CNTR>Co-op";  // the game's own row labels are centred the same way (commonmsg 0x8a, 0x8b)

GuiStep g_originalMove = nullptr;
GuiStep g_originalLoaded = nullptr;
bool g_faulted = false;

// Game thread only.
uintptr_t g_gui = 0;           // the uGUIPause the clone belongs to
uintptr_t g_coopRow = 0;       // the cloned row instance
int g_row = rule::kBack;
uint32_t g_lastState = 0;
bool g_openWanted = false;     // Co-op was chosen: the page opens once the pause has closed
uintptr_t g_openManager = 0;   // the Options screen showing the page, until its closed flag

uintptr_t field(uintptr_t base, uintptr_t offset) { return base ? game::readPointer(base + offset) : 0; }

uintptr_t objectOf(uintptr_t instance, size_t index) {
    const uintptr_t objects = field(instance, game::kInstanceObjectsOffset);
    return objects ? game::readPointer(objects + index * sizeof(uint32_t)) : 0;
}

void setY(uintptr_t instance, float y) {
    if (instance) game::callThiscall<void>(game::kNullSetPositionYFunction, reinterpret_cast<void*>(instance), y);
}

void setVisible(uintptr_t instance, bool visible) {
    if (instance) game::callThiscall<void>(game::kInstanceSetVisibleFunction, reinterpret_cast<void*>(instance), visible);
}

uintptr_t rowInstance(uintptr_t gui, int row) {
    switch (row) {
        case rule::kBack: return field(gui, game::kPauseExitOffset);
        case rule::kCoop: return g_coopRow;
        default: return field(gui, game::kPauseResetOffset);
    }
}

uintptr_t rowTouch(uintptr_t gui, int row) {
    switch (row) {
        case rule::kBack: return field(gui, game::kPauseExitTouchOffset);
        case rule::kCoop: return objectOf(g_coopRow, game::kRowTouchObject);
        default: return field(gui, game::kPauseResetTouchOffset);
    }
}

// The layout's two rows and their help line, one row apart from a copy of Quit Game labelled Co-op.
void addRow(uintptr_t gui) {
    const uintptr_t reset = field(gui, game::kPauseResetOffset);
    const uintptr_t parent = field(reset, game::kInstanceParentOffset);
    if (!reset || !parent) {
        logger::write("pause_coop: the pause layout has no Quit Game row, no Co-op row");
        return;
    }
    const uintptr_t clone = game::callThiscall<uintptr_t>(game::kCloneInstanceFunction, reinterpret_cast<void*>(gui), reset);
    const uintptr_t label = objectOf(clone, game::kRowLabelObject);
    if (!clone || !label) {
        logger::write("pause_coop: the Quit Game row could not be copied, no Co-op row");
        return;
    }
    game::callThiscall<void>(game::kAddChildFunction, reinterpret_cast<void*>(parent), clone);
    game::callThiscall<void>(game::kInstanceMarkFunction, reinterpret_cast<void*>(clone), uint8_t{1});
    game::callThiscall<void>(game::kObjectSetStringFunction, reinterpret_cast<void*>(label), kCoopLabel);
    g_gui = gui;
    g_coopRow = clone;
    setY(field(gui, game::kPauseExitOffset), rule::rowY(rule::kBack));
    setY(clone, rule::rowY(rule::kCoop));
    setY(reset, rule::rowY(rule::kQuit));
    setY(field(gui, game::kPauseHelpOffset), rule::kHelpY);
    logger::write("pause_coop: Co-op row added to the pause menu");
}

// The arrow on `row`; the help line names Back and Quit Game (the game's ids), and hides on Co-op.
void showRow(uintptr_t gui, int row) {
    setY(field(gui, game::kPauseArrowOffset), rule::rowY(row));
    setVisible(field(gui, game::kPauseHelpOffset), row != rule::kCoop);
}

void setState(uintptr_t gui, uint32_t state) {
    game::writeMemory(gui + game::kPauseStateOffset, state);
    game::writeMemory(gui + game::kPauseSubStateOffset, uint32_t{0});
}

rule::Input readInput(uintptr_t gui) {
    const uint32_t trigger = gui_input::trigger();
    const uint32_t repeat = gui_input::repeat();
    const uintptr_t keyboard = game::readPointer(game::kKeyboardGlobal);
    uint32_t keys = 0;
    if (keyboard) game::readMemory(keyboard + game::kKeyboardWordOffset, keys);
    rule::Input in;
    in.cancel = (trigger & (gui_input::padMask(game::kPadCancelMaskOffset) | game::kPadPauseCancelBit)) != 0 ||
                (keys & game::kKeyboardEscapeBit) != 0;
    in.decide = (trigger & gui_input::padMask(game::kPadDecideMaskOffset)) != 0;
    in.up = (repeat & game::kRepeatUp) != 0;
    in.down = (repeat & game::kRepeatDown) != 0;
    float position[gui_input::kPositionFloats] = {};
    if (!gui_input::mouseClicked(position)) return in;
    for (int row = 0; row < rule::kRowCount; ++row) {
        if (gui_input::hitTest(gui, rowInstance(gui, row), rowTouch(gui, row), position)) in.clickedRow = row;
    }
    return in;
}

void resume() {
    const uintptr_t pause = game::readPointer(game::kGamePauseGlobal);
    if (pause) game::callThiscall<void>(game::kGamePauseResumeFunction, reinterpret_cast<void*>(pause), uint8_t{1});
}

// State 3 of the pause, for three rows: what 0x5ccc60 does for two (sounds, resume, quit dialog, close), plus Co-op.
void input(uintptr_t gui) {
    const rule::Step step = rule::step(g_row, readInput(gui));
    g_row = step.row;
    switch (step.action) {
        case rule::Action::None: return;
        case rule::Action::Move:
            gui_input::playSound(game::kSoundMove);
            showRow(gui, g_row);
            return;
        case rule::Action::Cancel:
            gui_input::playSound(game::kSoundCancel);
            setState(gui, game::kPauseStateClose);
            return;
        case rule::Action::Quit:
            gui_input::playSound(game::kSoundDecide);
            setState(gui, game::kPauseStateQuitDialog);
            return;
        case rule::Action::Coop: g_openWanted = true; [[fallthrough]];
        case rule::Action::Back:
            gui_input::playSound(game::kSoundDecide);
            resume();
            setState(gui, game::kPauseStateClose);
            return;
    }
}

void move(uintptr_t gui) {
    uint32_t state = 0;
    const bool ours = gui == g_gui && g_coopRow && game::readMemory(gui + game::kPauseStateOffset, state) && state == game::kPauseStateInput;
    if (!ours) {
        g_lastState = state;
        g_originalMove(reinterpret_cast<void*>(gui), nullptr);
        return;
    }
    reinterpret_cast<GuiStep>(game::kGameGuiMoveFunction)(reinterpret_cast<void*>(gui), nullptr);
    uint8_t loaded = 0;
    if (!game::readMemory(gui + game::kGuiLoadedOffset, loaded) || !loaded) return;
    if (g_lastState != game::kPauseStateInput) {
        g_row = rule::kBack;  // the pause's open put its cursor and arrow on Back
        showRow(gui, g_row);
    }
    g_lastState = state;
    input(gui);
}

void setStop(uint32_t reason, bool on) {
    void* units = reinterpret_cast<void*>(game::readPointer(game::kUnitSystemGlobal));
    if (units) game::callThiscall<void>(game::kSetStopReasonFunction, units, reason, static_cast<uint32_t>(on));
}

// The world held the way the status screen holds it, and the Options manager's unit moving and drawn the way the status
// screen switches it on for its Options (sSubMenu 0x5d8040).
void holdWorld(uintptr_t manager, bool on) {
    setStop(live_menu_rule::kStopScreen, on);
    setStop(live_menu_rule::kStopScreenMore, on);
    uint32_t flags = 0;
    if (!game::readMemory(manager + game::kUnitFlagsOffset, flags)) return;
    const uint32_t unitOn = game::kUnitMoveFlag | game::kUnitDrawFlag;
    game::writeMemory(manager + game::kUnitFlagsOffset, on ? flags | unitOn : flags & ~unitOn);
}

void frame() {
    if (g_openManager) {
        if (game::callThiscall<bool>(game::kOptionTakeClosedFunction, reinterpret_cast<void*>(g_openManager))) {
            holdWorld(g_openManager, false);
            g_openManager = 0;
            logger::write("pause_coop: Co-op page closed, back to the game");
        }
        return;
    }
    if (!g_openWanted) return;
    const uintptr_t pause = game::readPointer(game::kGamePauseGlobal);
    uint32_t pauseState = 0;
    if (!pause || !game::readMemory(pause + game::kGamePauseStateOffset, pauseState) || pauseState != 0) return;
    g_openWanted = false;
    const uintptr_t manager = field(game::readPointer(game::kSubMenuGlobal), game::kSubMenuOptionsOffset);
    if (!manager || !native_page::openNext(coop_menu_rule::kInGameMenuId)) {
        logger::write("pause_coop: no in-game Options screen, Co-op page not opened");
        return;
    }
    holdWorld(manager, true);
    reinterpret_cast<GuiStep>(game::kOptionOpenFunction)(reinterpret_cast<void*>(manager), nullptr);
    g_openManager = manager;
    logger::write("pause_coop: Co-op page opening");
}

// A fault in our part gives the game its own two-row pause back for the session.
void __fastcall moveDetour(void* gui, void* edx) {
    if (!g_faulted) {
        __try {
            move(reinterpret_cast<uintptr_t>(gui));
            return;
        } __except (crash_dump::reportCaught(GetExceptionInformation(), "pause_coop: move")) {
            g_faulted = true;
            g_coopRow = 0;
            logger::write("pause_coop: fault, the pause menu is the game's own again");
        }
    }
    g_originalMove(gui, edx);
}

void __fastcall loadedDetour(void* gui, void* edx) {
    g_originalLoaded(gui, edx);
    if (g_faulted) return;
    __try {
        addRow(reinterpret_cast<uintptr_t>(gui));
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "pause_coop: row")) {
        g_faulted = true;
        g_coopRow = 0;
    }
}

bool patchSlot(size_t slot, uintptr_t expected, void* detour, GuiStep& original) {
    const uintptr_t address = game::kPauseGuiVtable + slot * sizeof(uint32_t);
    if (game::readPointer(address) != expected) return false;
    original = reinterpret_cast<GuiStep>(expected);
    return game::writeProtected(address, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(detour)));
}

}  // namespace

namespace pause_coop {

bool enable() {
    if (!patchSlot(game::kGuiMoveSlot, game::kPauseGuiMoveFunction, reinterpret_cast<void*>(&moveDetour), g_originalMove) ||
        !patchSlot(game::kGuiLoadedSlot, game::kPauseGuiLoadedFunction, reinterpret_cast<void*>(&loadedDetour), g_originalLoaded)) {
        logger::write("pause_coop: uGUIPause's vtable is not the expected one, no Co-op row");
        return false;
    }
    return debug_overlay::addFrameStep(onFrame);
}

void onFrame() {
    if (g_faulted) return;
    __try {
        frame();
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "pause_coop: frame")) {
        g_faulted = true;
        if (g_openManager) holdWorld(g_openManager, false);
        g_openManager = 0;
    }
}

}  // namespace pause_coop
