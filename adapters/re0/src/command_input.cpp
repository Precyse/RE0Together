#include "command_input.h"

#include <windows.h>

#include "camera_parity.h"
#include "character_owner.h"
#include "command_log.h"
#include "debug_stats.h"
#include "game.h"
#include "game_state.h"
#include "key_config.h"
#include "net_pad.h"
#include "pad_commands.h"
#include "party_mode.h"
#include "virtual_keys.h"
#include "window_focus.h"

namespace {

constexpr int kKeyDownMask = 0x8000;
constexpr UINT kExtendedScanPrefix = 0xE000;
constexpr uint8_t kDikExtendedBit = 0x80;
constexpr UINT kScanCodeMask = 0xFF;

key_config::CommandKeys g_keys{};
bool g_changeWasDown = false;  // net thread only: keyboard or controller held at the last poll
bool g_traceWasDown = false;
virtual_keys::HiddenKeys g_commandScancodes{};  // the switch and partner keys as DirectInput codes

// DirectInput key code of a virtual key: its scan code, with the high bit for extended keys (arrows).
uint8_t dikOf(int virtualKey) {
    const UINT scan = MapVirtualKeyW(static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC_EX);
    const bool extended = (scan & ~kScanCodeMask) == kExtendedScanPrefix;
    return static_cast<uint8_t>((scan & kScanCodeMask) | (extended ? kDikExtendedBit : 0));
}

bool keyDown(int virtualKey) { return (GetAsyncKeyState(virtualKey) & kKeyDownMask) != 0; }

// True on the poll where the command goes down (key or controller button). The edge state follows the raw input on
// every poll, so it cannot stick.
bool pressedEdge(bool down, bool& wasDown) {
    const bool edge = down && !wasDown;
    wasDown = down;
    return edge;
}

void publishFocus() {
    const auto focused = character_owner::identify(game::controlled());
    debug_stats::set(debug_stats::Gauge::FocusedCharacter,
                     focused == character_owner::Character::Unknown ? debug_stats::kUnknownOwner
                                                                    : static_cast<int>(focused));
}

// With a peer, in gameplay, the adapter owns both commands: the game's own switch would take this machine's camera,
// and its partner command would act on the other player's character. In menus and other screens the keys and buttons
// keep the game's own meaning.
bool commandsOwned() { return net_pad::active() && game_state::playing(); }

// Logs the press; true when it should become a request (game window in front, a peer connected, in gameplay).
bool accept(const char* name, int virtualKey, bool foreground, bool owned) {
    command_log::press(name, virtualKey, foreground);
    if (!foreground) {
        command_log::decide("%s ignored: window not foreground", name);
        return false;
    }
    if (!net_pad::active()) {
        command_log::decide("%s ignored: no peer connected", name);
        return false;
    }
    if (!owned) {
        command_log::decide("%s ignored: not in gameplay", name);
        return false;
    }
    debug_stats::count(debug_stats::Counter::CommandsSent);
    return true;
}

}  // namespace

namespace command_input {

void enable() {
    g_keys = key_config::load();
    g_commandScancodes = {dikOf(g_keys.change), dikOf(g_keys.trace)};
}

void onNetTick() {
    publishFocus();
    const bool owned = commandsOwned();
    virtual_keys::setHiddenKeys(owned ? g_commandScancodes : virtual_keys::HiddenKeys{});
    pad_commands::setHidden(owned);
    const bool foreground = gameIsForeground();
    const pad_commands::Buttons pad = pad_commands::pressed();
    const bool change = pressedEdge(keyDown(g_keys.change) || pad.change, g_changeWasDown);
    const bool trace = pressedEdge(keyDown(g_keys.trace) || pad.trace, g_traceWasDown);
    if (change && accept("switch", g_keys.change, foreground, owned)) camera_parity::onLocalSwitchKey();
    if (trace && accept("party", g_keys.trace, foreground, owned)) party_mode::onLocalToggleKey();
}

}  // namespace command_input
