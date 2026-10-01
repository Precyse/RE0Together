#include "pad_commands.h"

#include <windows.h>
#include <xinput.h>

#include <array>
#include <atomic>

#include "game.h"
#include "log.h"

namespace {

constexpr uintptr_t kGetStateImportSlot = 0xcb13b0;  // XINPUT1_3 ordinal 2
constexpr WORD kChangeButton = XINPUT_GAMEPAD_Y;
constexpr uint8_t kChangeBit = 1;
constexpr uint8_t kTraceBit = 2;

using GetStateFn = DWORD(WINAPI*)(DWORD index, XINPUT_STATE* state);

GetStateFn g_original = nullptr;
std::atomic<bool> g_hidden{false};
std::array<std::atomic<uint8_t>, XUSER_MAX_COUNT> g_pressed{};  // per controller: kChangeBit | kTraceBit

DWORD WINAPI getStateFilter(DWORD index, XINPUT_STATE* state) {
    const DWORD result = g_original(index, state);
    if (index >= XUSER_MAX_COUNT) return result;
    if (result != ERROR_SUCCESS || !state) {
        g_pressed[index] = 0;  // unplugged: nothing stays held
        return result;
    }
    XINPUT_GAMEPAD& pad = state->Gamepad;
    const bool change = (pad.wButtons & kChangeButton) != 0;
    const bool trace = pad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    g_pressed[index] = static_cast<uint8_t>((change ? kChangeBit : 0) | (trace ? kTraceBit : 0));
    if (g_hidden) {
        pad.wButtons &= ~kChangeButton;
        pad.bLeftTrigger = 0;
    }
    return result;
}

}  // namespace

namespace pad_commands {

bool install() {
    const uintptr_t original = game::readPointer(kGetStateImportSlot);
    if (!original) {
        logger::write("pad_commands: XInputGetState import is empty, controller commands unavailable");
        return false;
    }
    g_original = reinterpret_cast<GetStateFn>(original);
    const bool patched = game::writeProtected(kGetStateImportSlot, reinterpret_cast<uint32_t>(&getStateFilter));
    logger::write("pad_commands: XInputGetState import %s", patched ? "filtered" : "could not be patched");
    return patched;
}

void uninstall() {
    if (g_original) game::writeProtected(kGetStateImportSlot, reinterpret_cast<uint32_t>(g_original));
}

void setHidden(bool hidden) { g_hidden = hidden; }

Buttons pressed() {
    uint8_t any = 0;
    for (const auto& bits : g_pressed) any |= bits;
    return {(any & kChangeBit) != 0, (any & kTraceBit) != 0};
}

}  // namespace pad_commands
