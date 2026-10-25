#include "title_coop.h"

#include <windows.h>

#include <array>
#include <cstdint>

#include "coop_menu_rule.h"
#include "crash_dump.h"
#include "game.h"
#include "hooks.h"
#include "lobby.h"
#include "log.h"
#include "native_page.h"
#include "title_coop_rule.h"
#include "title_label.h"
#include "title_load.h"

namespace {

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
bool g_faulted = false;    // game thread only: a fault in our step switched the entry off for the session
uint32_t g_lastState = 0;  // the title's state on the previous frame

uint32_t stateOf(uintptr_t title) {
    uint32_t state = 0;
    game::readMemory(title + game::kTitleStateOffset, state);
    return state;
}

void setState(uintptr_t title, uint32_t state) { game::writeMemory(title + game::kTitleStateOffset, state); }

// On every opening of the main menu: the fifth row is shown and its atlas cell gets our label.
void prepareMenu(uintptr_t title) {
    game::writeMemory(title + game::kTitleFiveRowsOffset, title_coop_rule::kFiveRows);
    if (g_lastState != title_coop_rule::kStateMainMenuOpen) title_label::publish(title);
}

void beforeMove(uintptr_t title) {
    const uint32_t state = stateOf(title);
    if (state == title_coop_rule::kStateMainMenuOpen) prepareMenu(title);
    g_lastState = state;
    if (state == title_coop_rule::kStateStoreChosen) {
        const bool page = native_page::openFromTitle(coop_menu_rule::kMenuId);
        if (!page) logger::write("title_coop: the Co-op page is unavailable, the main menu stays");
        setState(title, title_coop_rule::afterCoopChoice(page));
    }
    title_load::titleBefore(title, stateOf(title));
    lobby::setPlace(title_coop_rule::placeOf(native_page::active(), title_load::busy()));
}

void guardedBefore(uintptr_t title) {
    __try {
        beforeMove(title);
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "title_coop: title step")) {
        g_faulted = true;
        game::writeMemory(title + game::kTitleFiveRowsOffset, uint8_t{0});  // the row's choice is the store again
        setState(title, title_coop_rule::kStateMainMenuOpen);
    }
}

void guardedAfter(uint32_t before, uint32_t after) {
    __try {
        title_load::titleAfter(before, after);
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "title_coop: after the title step")) {
        g_faulted = true;
    }
}

void __fastcall moveDetour(void* self, void* edx) {
    const auto title = reinterpret_cast<uintptr_t>(self);
    if (!g_faulted) guardedBefore(title);
    const uint32_t before = stateOf(title);
    g_originalMove(self, edx);
    if (!g_faulted) guardedAfter(before, stateOf(title));
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
