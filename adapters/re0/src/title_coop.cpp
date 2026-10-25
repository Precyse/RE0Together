#include "title_coop.h"

#include <windows.h>

#include <array>
#include <cstdint>

#include "coop_page.h"
#include "crash_dump.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "title_coop_rule.h"
#include "title_label.h"

namespace {

using title_coop_rule::Step;

// The title's move prologue, its dispatch bound, the main menu's row count and the fifth row's choice, as read from
// the image.
constexpr std::array<uint8_t, 4> kMoveBytes = {0x56, 0x8b, 0xf1, 0xe8};           // push esi; mov esi, ecx; call
constexpr std::array<uint8_t, 5> kDispatchBytes = {0x83, 0xf8, 0x3a, 0x0f, 0x87};  // cmp eax, 0x3a; ja
// cmp byte [esi+0x27a], 1
constexpr std::array<uint8_t, 7> kFiveRowsBytes = {0x80, 0xbe, 0x7a, 0x02, 0x00, 0x00, 0x01};
// mov dword [esi+0x27c], 0x2b
constexpr std::array<uint8_t, 10> kStoreChoiceBytes = {0xc7, 0x86, 0x7c, 0x02, 0x00, 0x00, 0x2b, 0x00, 0x00, 0x00};

using FastcallFunction = void(__fastcall*)(void* self, void* edx);

FastcallFunction g_originalMove = nullptr;
bool g_pageOpen = false;    // game thread only, like everything below
bool g_faulted = false;     // a fault in our step switched the entry off for the session
uint32_t g_lastState = 0;   // the title's state on the previous frame

void setState(uintptr_t title, uint32_t state) { game::writeMemory(title + game::kTitleStateOffset, state); }

// On every opening of the main menu: the fifth row is shown and its atlas cell gets our label.
void prepareMenu(uintptr_t title) {
    game::writeMemory(title + game::kTitleFiveRowsOffset, title_coop_rule::kFiveRows);
    if (g_lastState != title_coop_rule::kStateMainMenuOpen) title_label::publish(title);
}

void titleStep(uintptr_t title) {
    uint32_t state = 0;
    if (!game::readMemory(title + game::kTitleStateOffset, state)) return;
    if (state == title_coop_rule::kStateMainMenuOpen) prepareMenu(title);
    g_lastState = state;

    const bool closeInput = g_pageOpen && state == title_coop_rule::kStateParked && coop_page::update();
    switch (title_coop_rule::step(state, g_pageOpen, closeInput)) {
        case Step::OpenPage:
            g_pageOpen = true;  // first, so a fault below still gives the menu back
            setState(title, title_coop_rule::kStateParked);
            coop_page::open();
            break;
        case Step::ClosePage:
            g_pageOpen = false;
            coop_page::close();
            setState(title, title_coop_rule::kStateMainMenuOpen);  // the cursor is still on Co-op
            break;
        case Step::DropPage:
            g_pageOpen = false;
            coop_page::close();
            break;
        case Step::KeepPage:
        case Step::None:
            break;
    }
}

void guardedTitleStep(uintptr_t title) {
    __try {
        titleStep(title);
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "title_coop: title step")) {
        g_faulted = true;
        game::writeMemory(title + game::kTitleFiveRowsOffset, uint8_t{0});  // the row's choice is the store again
        if (!g_pageOpen) return;
        g_pageOpen = false;
        coop_page::close();
        setState(title, title_coop_rule::kStateMainMenuOpen);
    }
}

void __fastcall moveDetour(void* self, void* edx) {
    if (!g_faulted) guardedTitleStep(reinterpret_cast<uintptr_t>(self));
    g_originalMove(self, edx);
}

bool sitesMatch() {
    return game::bytesMatch(game::kTitleMoveFunction, kMoveBytes) &&
           game::bytesMatch(game::kTitleDispatchCheck, kDispatchBytes) &&
           game::bytesMatch(game::kTitleFiveRowsRead, kFiveRowsBytes) &&
           game::bytesMatch(game::kTitleStoreChoice, kStoreChoiceBytes);
}

}  // namespace

namespace title_coop {

bool enable() {
    if (!sitesMatch()) {
        logger::write("title_coop: the title screen is not the expected code, entry not added");
        return false;
    }
    return hooks::install("uGUITitle::v8", game::kTitleMoveFunction, reinterpret_cast<void*>(&moveDetour),
                          reinterpret_cast<void**>(&g_originalMove));
}

void uninstall() { hooks::remove(game::kTitleMoveFunction); }

}  // namespace title_coop
